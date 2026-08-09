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

bool has_valid_garment_layouts(const SimulationGpuView& views)
{
    if (!is_valid_motion_view(views.cloth_motion) || !is_valid_cloth_bvh_buffer_view(views.cloth_bvh)) {
        return false;
    }

    const auto garment_count = static_cast<std::size_t>(
        std::count_if(views.garment_vertex_ranges.begin(),
                      views.garment_vertex_ranges.end(),
                      [](const ElementRange& vertex_range) { return vertex_range.count != 0u; }));
    if (views.cloth_bvh.garment_layouts->size() != garment_count) {
        return false;
    }

    std::unordered_set<GarmentLayer> layers;
    for (const GarmentBvhLayout& layout : *views.cloth_bvh.garment_layouts) {
        const GarmentBvhRange& bvh_range = layout.range;
        const ElementRange& vertex_range = views.garment_vertex_ranges[bvh_range.layer];
        if (!layers.insert(bvh_range.layer).second ||
            !is_valid_range(vertex_range.offset, vertex_range.count, views.cloth_motion.vertex_count) ||
            !is_valid_range(bvh_range.collision_triangles.offset,
                            bvh_range.collision_triangles.count,
                            views.cloth_bvh.triangle_count) ||
            !is_valid_range(bvh_range.bvh_nodes.offset,
                            bvh_range.bvh_nodes.count,
                            views.cloth_bvh.node_count)) {
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
    candidate_detect_.program =
        load_compute_program(candidate_detect_shader_path, "Cloth-cloth vertex-face candidate detection", gl);
    dispatch_size_.program =
        load_compute_program(dispatch_size_shader_path, "Cloth-cloth candidate dispatch size", gl);
    if (!is_initialized()) {
        release(gl);
        return false;
    }

    candidate_detect_.upper_vertex_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexOffset");
    candidate_detect_.upper_vertex_count =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexCount");
    candidate_detect_.upper_triangle_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperTriangleOffset");
    candidate_detect_.upper_bvh_node_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperBvhNodeOffset");
    candidate_detect_.lower_vertex_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexOffset");
    candidate_detect_.lower_vertex_count =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexCount");
    candidate_detect_.lower_triangle_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerTriangleOffset");
    candidate_detect_.lower_bvh_node_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerBvhNodeOffset");
    candidate_detect_.max_candidates =
        gl.glGetUniformLocation(candidate_detect_.program, "uMaxCandidateCount");
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

    return true;
}

bool ClothClothCollisionDetector::can_detect(const SimulationGpuView& views) const
{
    if (!is_initialized() || !has_valid_garment_layouts(views)) {
        return false;
    }

    const auto garment_count = static_cast<std::uint32_t>(views.cloth_bvh.garment_layouts->size());
    if (garment_count < 2u) {
        return true;
    }

    std::uint32_t required_capacity = 0;
    return CollisionCandidateBuffers::calculate_cloth_cloth_candidate_capacity(
               views.cloth_motion.vertex_count,
               garment_count,
               required_capacity) &&
           is_valid_cloth_cloth_candidate_buffer_view(views.collision_candidates) &&
           views.collision_candidates.vertex_capacity >= views.cloth_motion.vertex_count &&
           views.collision_candidates.cloth_cloth_vertex_face.capacity >= required_capacity;
}

void ClothClothCollisionDetector::detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
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

    views.collision_candidates.clear_cloth_cloth_candidate_counts(gl);

    gl.glUseProgram(candidate_detect_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::collision_triangles,
                        views.cloth_bvh.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::triangle_bounds,
                        views.cloth_bvh.triangle_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::bvh_nodes,
                        views.cloth_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::overflow_count,
                        collision_candidates.overflow_count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.max_candidates,
                           collision_candidates.capacity);

    const std::vector<GarmentBvhLayout>& layouts = *views.cloth_bvh.garment_layouts;
    for (std::size_t first_index = 0; first_index < layouts.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1u; second_index < layouts.size(); ++second_index) {
            const GarmentBvhLayout* upper_layout = &layouts[first_index];
            const GarmentBvhLayout* lower_layout = &layouts[second_index];
            if (upper_layout->range.layer < lower_layout->range.layer) {
                std::swap(upper_layout, lower_layout);
            }

            const ElementRange& upper_vertex_range = views.garment_vertex_ranges[upper_layout->range.layer];
            const ElementRange& lower_vertex_range = views.garment_vertex_ranges[lower_layout->range.layer];

            detect_pair(upper_vertex_range,
                        *upper_layout,
                        lower_vertex_range,
                        *lower_layout,
                        collision_candidates,
                        gl);
        }
    }

    build_dispatch_size(collision_candidates, gl);
}

void ClothClothCollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(dispatch_size_.program);
    gl.glDeleteProgram(candidate_detect_.program);
    candidate_detect_ = {};
    dispatch_size_ = {};
}

void ClothClothCollisionDetector::detect_pair(const ElementRange& upper_vertex_range,
                                              const GarmentBvhLayout& upper_layout,
                                              const ElementRange& lower_vertex_range,
                                              const GarmentBvhLayout& lower_layout,
                                              const CollisionCandidateBuffer& collision_candidates,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_vertex_offset,
                           upper_vertex_range.offset);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_vertex_count,
                           upper_vertex_range.count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_triangle_offset,
                           upper_layout.range.collision_triangles.offset);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_bvh_node_offset,
                           upper_layout.range.bvh_nodes.offset);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_vertex_offset,
                           lower_vertex_range.offset);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_vertex_count,
                           lower_vertex_range.count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_triangle_offset,
                           lower_layout.range.collision_triangles.offset);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_bvh_node_offset,
                           lower_layout.range.bvh_nodes.offset);

    const std::uint32_t query_vertex_count = upper_vertex_range.count + lower_vertex_range.count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionDetector::build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::dispatch_size,
                        collision_candidates.dispatch_size);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.max_candidates,
                           collision_candidates.capacity);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.local_size,
                           candidate_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}
