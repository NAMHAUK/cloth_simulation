#include "simulation/collision/ClothClothCollisionDetector.h"

#include "gpu/scene/CollisionPairBuffers.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>

namespace {
constexpr std::uint32_t pair_detect_local_size = 128u;
constexpr std::uint32_t pair_accumulate_local_size = 128u;
constexpr std::uint32_t diagnostic_log_interval = 100u;
constexpr std::uint32_t higher_vertex_lower_face_role = 0u;
constexpr std::uint32_t lower_vertex_higher_face_role = 1u;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace pair_detect_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint collision_triangles = 2;
constexpr GLuint triangle_bounds = 3;
constexpr GLuint bvh_nodes = 4;
constexpr GLuint pair_records = 5;
constexpr GLuint pair_count = 6;
constexpr GLuint overflow_count = 7;
}

namespace dispatch_size_binding {
constexpr GLuint pair_count = 0;
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
    return pair_detect_.program != 0 && dispatch_size_.program != 0;
}

bool ClothClothCollisionDetector::initialize(const std::filesystem::path& pair_detect_shader_path,
                                             const std::filesystem::path& dispatch_size_shader_path,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    pair_detect_.program = load_compute_program(pair_detect_shader_path,
                                                "Cloth-cloth vertex-face pair detection",
                                                gl);
    dispatch_size_.program = load_compute_program(dispatch_size_shader_path,
                                                  "Cloth-cloth pair dispatch size",
                                                  gl);
    if (!is_initialized()) {
        release(gl);
        return false;
    }

    pair_detect_.query_vertex_offset = gl.glGetUniformLocation(pair_detect_.program, "uQueryVertexOffset");
    pair_detect_.query_vertex_count = gl.glGetUniformLocation(pair_detect_.program, "uQueryVertexCount");
    pair_detect_.query_bvh_node_offset = gl.glGetUniformLocation(pair_detect_.program, "uQueryBvhNodeOffset");
    pair_detect_.target_vertex_offset = gl.glGetUniformLocation(pair_detect_.program, "uTargetVertexOffset");
    pair_detect_.target_triangle_offset = gl.glGetUniformLocation(pair_detect_.program, "uTargetTriangleOffset");
    pair_detect_.target_bvh_node_offset = gl.glGetUniformLocation(pair_detect_.program, "uTargetBvhNodeOffset");
    pair_detect_.query_role = gl.glGetUniformLocation(pair_detect_.program, "uQueryRole");
    pair_detect_.max_pairs = gl.glGetUniformLocation(pair_detect_.program, "uMaxPairCount");
    dispatch_size_.max_pairs = gl.glGetUniformLocation(dispatch_size_.program, "uMaxPairCount");
    dispatch_size_.local_size = gl.glGetUniformLocation(dispatch_size_.program, "uLocalSize");

    if (pair_detect_.query_vertex_offset < 0 ||
        pair_detect_.query_vertex_count < 0 ||
        pair_detect_.query_bvh_node_offset < 0 ||
        pair_detect_.target_vertex_offset < 0 ||
        pair_detect_.target_triangle_offset < 0 ||
        pair_detect_.target_bvh_node_offset < 0 ||
        pair_detect_.query_role < 0 ||
        pair_detect_.max_pairs < 0 ||
        dispatch_size_.max_pairs < 0 ||
        dispatch_size_.local_size < 0) {
        std::cerr << "Cloth-cloth pair detection compute shader missing required uniforms.\n";
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
    return CollisionPairBuffers::calculate_cloth_cloth_pair_capacity(views.cloth_motion.vertex_count,
                                                                    garment_count,
                                                                    required_capacity) &&
           is_valid_cloth_cloth_pair_buffer_view(views.collision_pairs) &&
           views.collision_pairs.vertex_capacity >= views.cloth_motion.vertex_count &&
           views.collision_pairs.cloth_cloth_vertex_face.capacity >= required_capacity;
}

void ClothClothCollisionDetector::detect(const SimulationGpuViews& views,
                                         QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    const CollisionPairBuffer& collision_pairs = views.collision_pairs.cloth_cloth_vertex_face;
    if (views.cloth_bvh.garment_layouts->size() < 2u) {
        if (is_valid_collision_pair_buffer(collision_pairs)) {
            views.collision_pairs.clear_cloth_cloth_pair_counts(gl);
            build_dispatch_size(collision_pairs, gl);
        }
        return;
    }

#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    const bool gpu_timing_started = detection_timer_.begin(gl);
#endif
    views.collision_pairs.clear_cloth_cloth_pair_counts(gl);

    gl.glUseProgram(pair_detect_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::collision_triangles, views.cloth_bvh.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::triangle_bounds, views.cloth_bvh.triangle_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::bvh_nodes, views.cloth_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::pair_records, collision_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::pair_count, collision_pairs.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, pair_detect_binding::overflow_count, collision_pairs.overflow_count);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.max_pairs, collision_pairs.capacity);

    const std::vector<GarmentBvhLayout>& layouts = *views.cloth_bvh.garment_layouts;
    for (std::size_t first_index = 0; first_index < layouts.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1u; second_index < layouts.size(); ++second_index) {
            const GarmentBvhLayout* higher_layout = &layouts[first_index];
            const GarmentBvhLayout* lower_layout = &layouts[second_index];
            if (higher_layout->range.layer < lower_layout->range.layer) {
                std::swap(higher_layout, lower_layout);
            }

            const GarmentBufferRanges* higher_range =
                find_garment_range(*views.garment_buffer_ranges, higher_layout->range.garment_id);
            const GarmentBufferRanges* lower_range =
                find_garment_range(*views.garment_buffer_ranges, lower_layout->range.garment_id);

            detect_direction(*higher_range,
                             *higher_layout,
                             *lower_range,
                             *lower_layout,
                             higher_vertex_lower_face_role,
                             collision_pairs,
                             gl);
            detect_direction(*lower_range,
                             *lower_layout,
                             *higher_range,
                             *higher_layout,
                             lower_vertex_higher_face_role,
                             collision_pairs,
                             gl);
        }
    }

    build_dispatch_size(collision_pairs, gl);
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
        !is_valid_collision_pair_buffer(views.collision_pairs.cloth_cloth_vertex_face)) {
        return;
    }

    const CollisionPairBuffer& collision_pairs = views.collision_pairs.cloth_cloth_vertex_face;
    std::uint32_t pair_count = 0;
    std::uint32_t overflow_count = 0;
    gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glGetNamedBufferSubData(collision_pairs.pair_count, 0, sizeof(pair_count), &pair_count);
    gl.glGetNamedBufferSubData(collision_pairs.overflow_count, 0, sizeof(overflow_count), &overflow_count);
    std::cerr << "[CLOTH-CLOTH] pairs " << pair_count
              << ", stored " << std::min(pair_count, collision_pairs.capacity)
              << ", overflow " << overflow_count
              << ", capacity " << collision_pairs.capacity << ".\n";
#else
    (void)views;
    (void)simulation_step;
    (void)gl;
#endif
}

void ClothClothCollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(dispatch_size_.program);
    gl.glDeleteProgram(pair_detect_.program);
#if CLOTH_SIM_CLOTH_CLOTH_COLLISION_GPU_TIMING
    detection_timer_.release(gl);
#endif
    pair_detect_ = {};
    dispatch_size_ = {};
}

void ClothClothCollisionDetector::detect_direction(
    const GarmentBufferRanges& query_range,
    const GarmentBvhLayout& query_layout,
    const GarmentBufferRanges& target_range,
    const GarmentBvhLayout& target_layout,
    std::uint32_t query_role,
    const CollisionPairBuffer& collision_pairs,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.query_vertex_offset, query_range.vertex_offset);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.query_vertex_count, query_range.vertex_count);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.query_bvh_node_offset, query_layout.range.bvh_nodes.offset);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.target_vertex_offset, target_range.vertex_offset);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.target_triangle_offset, target_layout.range.collision_triangles.offset);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.target_bvh_node_offset, target_layout.range.bvh_nodes.offset);
    gl.glProgramUniform1ui(pair_detect_.program, pair_detect_.query_role, query_role);
    gl.glDispatchCompute(compute_group_count(query_range.vertex_count, pair_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionDetector::build_dispatch_size(
    const CollisionPairBuffer& collision_pairs,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_size_binding::pair_count, collision_pairs.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_size_binding::dispatch_size, collision_pairs.dispatch_size);
    gl.glProgramUniform1ui(dispatch_size_.program, dispatch_size_.max_pairs, collision_pairs.capacity);
    gl.glProgramUniform1ui(dispatch_size_.program, dispatch_size_.local_size, pair_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}
