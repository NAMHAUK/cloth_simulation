#include "simulation/collision/ClothClothCollisionDetector.h"

#include "gpu/scene/CollisionCandidateBuffers.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
constexpr std::uint32_t candidate_accumulate_local_size = 128u;
constexpr std::uint32_t diagnostic_log_interval = 100u;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace candidate_detect_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint collision_triangles = 2;
constexpr GLuint triangle_bounds = 3;
constexpr GLuint bvh_nodes = 4;
constexpr GLuint candidates = 5;
constexpr GLuint candidate_count = 6;
constexpr GLuint overflow_count = 7;
}

namespace dispatch_size_binding {
constexpr GLuint candidate_count = 0;
constexpr GLuint dispatch_size = 1;
}

bool is_valid_range(std::uint32_t offset, std::uint32_t count, std::uint32_t total_count)
{
    return count != 0u && offset <= total_count && count <= total_count - offset;
}

const GarmentBufferRanges* find_garment_range(
    const std::vector<GarmentBufferRanges>& garment_ranges,
    std::uint32_t garment_id)
{
    const auto iter = std::find_if(garment_ranges.begin(), garment_ranges.end(),
        [garment_id](const GarmentBufferRanges& range) {
            return range.id == garment_id;
        });
    return iter == garment_ranges.end() ? nullptr : &(*iter);
}

bool has_valid_garment_layouts(const SimulationGpuViews& views)
{
    if (!is_valid_motion_view(views.cloth_motion) ||
        !is_valid_cloth_bvh_buffer_view(views.cloth_bvh) ||
        views.garment_buffer_ranges == nullptr ||
        views.cloth_bvh.garment_layouts->size() != views.garment_buffer_ranges->size()) {
        return false;
    }

    std::unordered_set<std::uint32_t> garment_ids;
    std::unordered_set<std::uint32_t> layers;
    for (const GarmentBvhLayout& layout : *views.cloth_bvh.garment_layouts) {
        const GarmentBvhRange& bvh_range = layout.range;
        const GarmentBufferRanges* garment_range =
            find_garment_range(*views.garment_buffer_ranges, bvh_range.garment_id);
        if (garment_range == nullptr ||
            !garment_ids.insert(bvh_range.garment_id).second ||
            !layers.insert(bvh_range.layer).second ||
            !is_valid_range(garment_range->vertex_offset,
                            garment_range->vertex_count,
                            views.cloth_motion.vertex_count) ||
            !is_valid_range(bvh_range.collision_triangles.offset,
                            bvh_range.collision_triangles.count,
                            views.cloth_bvh.triangle_count) ||
            !is_valid_range(bvh_range.bvh_nodes.offset,
                            bvh_range.bvh_nodes.count,
                            views.cloth_bvh.node_count) ||
            bvh_range.collision_triangles.count != garment_range->triangle_count) {
            return false;
        }
    }

    return true;
}
}

bool ClothClothCollisionDetector::is_initialized() const
{
    return candidate_detect_.program != 0 && dispatch_size_.program != 0;
}

bool ClothClothCollisionDetector::initialize(const std::filesystem::path& candidate_detect_shader_path,
                                             const std::filesystem::path& dispatch_size_shader_path,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    candidate_detect_.program = load_compute_program(candidate_detect_shader_path,
                                                "Cloth-cloth vertex-face candidate detection",
                                                gl);
    dispatch_size_.program = load_compute_program(dispatch_size_shader_path,
                                                  "Cloth-cloth candidate dispatch size",
                                                  gl);
    if (!is_initialized()) {
        release(gl);
        return false;
    }

    candidate_detect_.upper_vertex_offset = gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexOffset");
    candidate_detect_.upper_vertex_count = gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexCount");
    candidate_detect_.upper_triangle_offset = gl.glGetUniformLocation(candidate_detect_.program, "uUpperTriangleOffset");
    candidate_detect_.upper_bvh_node_offset = gl.glGetUniformLocation(candidate_detect_.program, "uUpperBvhNodeOffset");
    candidate_detect_.lower_vertex_offset = gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexOffset");
    candidate_detect_.lower_vertex_count = gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexCount");
    candidate_detect_.lower_triangle_offset = gl.glGetUniformLocation(candidate_detect_.program, "uLowerTriangleOffset");
    candidate_detect_.lower_bvh_node_offset = gl.glGetUniformLocation(candidate_detect_.program, "uLowerBvhNodeOffset");
    candidate_detect_.max_candidates = gl.glGetUniformLocation(candidate_detect_.program, "uMaxCandidateCount");
    dispatch_size_.max_candidates = gl.glGetUniformLocation(dispatch_size_.program, "uMaxCandidateCount");
    dispatch_size_.local_size = gl.glGetUniformLocation(dispatch_size_.program, "uLocalSize");

    if (candidate_detect_.upper_vertex_offset < 0 ||
        candidate_detect_.upper_vertex_count < 0 ||
        candidate_detect_.upper_triangle_offset < 0 ||
        candidate_detect_.upper_bvh_node_offset < 0 ||
        candidate_detect_.lower_vertex_offset < 0 ||
        candidate_detect_.lower_vertex_count < 0 ||
        candidate_detect_.lower_triangle_offset < 0 ||
        candidate_detect_.lower_bvh_node_offset < 0 ||
        candidate_detect_.max_candidates < 0 ||
        dispatch_size_.max_candidates < 0 ||
        dispatch_size_.local_size < 0) {
        std::cerr << "Cloth-cloth candidate detection compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    detection_timer_.initialize("cloth-cloth vertex-face pair detection", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool ClothClothCollisionDetector::can_detect(const SimulationGpuViews& views) const
{
    if (!is_initialized() || !has_valid_garment_layouts(views)) {
        return false;
    }

    const auto garment_count = static_cast<std::uint32_t>(views.cloth_bvh.garment_layouts->size());
    if (garment_count < 2u) {
        return true;
    }

    std::uint32_t required_capacity = 0;
    return CollisionCandidateBuffers::calculate_cloth_cloth_candidate_capacity(views.cloth_motion.vertex_count,
                                                                    garment_count,
                                                                    required_capacity) &&
           is_valid_cloth_cloth_candidate_buffer_view(views.collision_candidates) &&
           views.collision_candidates.vertex_capacity >= views.cloth_motion.vertex_count &&
           views.collision_candidates.cloth_cloth_vertex_face.capacity >= required_capacity;
}

void ClothClothCollisionDetector::detect(const SimulationGpuViews& views,
                                         QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    if (views.cloth_bvh.garment_layouts->size() < 2u) {
        if (is_valid_collision_candidate_buffer(collision_candidates)) {
            views.collision_candidates.clear_cloth_cloth_candidate_counts(gl);
            build_dispatch_size(collision_candidates, gl);
        }
        return;
    }

#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    const bool gpu_timing_started = detection_timer_.begin(gl);
#endif
    views.collision_candidates.clear_cloth_cloth_candidate_counts(gl);

    gl.glUseProgram(candidate_detect_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::collision_triangles, views.cloth_bvh.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::triangle_bounds, views.cloth_bvh.triangle_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::bvh_nodes, views.cloth_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::candidates, collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::candidate_count, collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, candidate_detect_binding::overflow_count, collision_candidates.overflow_count);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.max_candidates, collision_candidates.capacity);

    const std::vector<GarmentBvhLayout>& layouts = *views.cloth_bvh.garment_layouts;
    for (std::size_t first_index = 0; first_index < layouts.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1u; second_index < layouts.size(); ++second_index) {
            const GarmentBvhLayout* upper_layout = &layouts[first_index];
            const GarmentBvhLayout* lower_layout = &layouts[second_index];
            if (upper_layout->range.layer < lower_layout->range.layer) {
                std::swap(upper_layout, lower_layout);
            }

            const GarmentBufferRanges* upper_range =
                find_garment_range(*views.garment_buffer_ranges, upper_layout->range.garment_id);
            const GarmentBufferRanges* lower_range =
                find_garment_range(*views.garment_buffer_ranges, lower_layout->range.garment_id);

            detect_pair(*upper_range,
                        *upper_layout,
                        *lower_range,
                        *lower_layout,
                        collision_candidates,
                        gl);
        }
    }

    build_dispatch_size(collision_candidates, gl);
#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        detection_timer_.end(gl);
    }
#endif
}

void ClothClothCollisionDetector::log_diagnostics(const SimulationGpuViews& views,
                                                  std::uint64_t simulation_step,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
#ifndef NDEBUG
    if (simulation_step % diagnostic_log_interval != 0u ||
        !is_valid_collision_candidate_buffer(views.collision_candidates.cloth_cloth_vertex_face)) {
        return;
    }

    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    std::uint32_t candidate_count = 0;
    std::uint32_t overflow_count = 0;
    gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glGetNamedBufferSubData(collision_candidates.candidate_count, 0, sizeof(candidate_count), &candidate_count);
    gl.glGetNamedBufferSubData(collision_candidates.overflow_count, 0, sizeof(overflow_count), &overflow_count);
    std::cerr << "[CLOTH-CLOTH] candidates " << candidate_count
              << ", stored " << std::min(candidate_count, collision_candidates.capacity)
              << ", overflow " << overflow_count
              << ", capacity " << collision_candidates.capacity << ".\n";
#else
    (void)views;
    (void)simulation_step;
    (void)gl;
#endif
}

void ClothClothCollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(dispatch_size_.program);
    gl.glDeleteProgram(candidate_detect_.program);
#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    detection_timer_.release(gl);
#endif
    candidate_detect_ = {};
    dispatch_size_ = {};
}

void ClothClothCollisionDetector::detect_pair(
    const GarmentBufferRanges& upper_range,
    const GarmentBvhLayout& upper_layout,
    const GarmentBufferRanges& lower_range,
    const GarmentBvhLayout& lower_layout,
    const CollisionCandidateBuffer& collision_candidates,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.upper_vertex_offset, upper_range.vertex_offset);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.upper_vertex_count, upper_range.vertex_count);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.upper_triangle_offset, upper_layout.range.collision_triangles.offset);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.upper_bvh_node_offset, upper_layout.range.bvh_nodes.offset);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.lower_vertex_offset, lower_range.vertex_offset);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.lower_vertex_count, lower_range.vertex_count);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.lower_triangle_offset, lower_layout.range.collision_triangles.offset);
    gl.glProgramUniform1ui(candidate_detect_.program, candidate_detect_.lower_bvh_node_offset, lower_layout.range.bvh_nodes.offset);

    const std::uint32_t query_vertex_count = upper_range.vertex_count + lower_range.vertex_count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionDetector::build_dispatch_size(
    const CollisionCandidateBuffer& collision_candidates,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_size_binding::candidate_count, collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_size_binding::dispatch_size, collision_candidates.dispatch_size);
    gl.glProgramUniform1ui(dispatch_size_.program, dispatch_size_.max_candidates, collision_candidates.capacity);
    gl.glProgramUniform1ui(dispatch_size_.program, dispatch_size_.local_size, candidate_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}
