#include "gpu/body/bvh/TriangleBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint character_triangle_geometry_binding = 0;
constexpr GLuint character_triangle_indices_binding = 1;
constexpr GLuint character_previous_positions_binding = 2;
constexpr GLuint character_bvh_node_binding = 3;
constexpr GLuint body_triangle_bounds_binding = 4;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;
constexpr std::uint32_t gpu_timing_log_interval = 100u;
}

bool TriangleBvhBoundsUpdater::is_initialized() const
{
    return program_ != 0;
}

bool TriangleBvhBoundsUpdater::can_update(const CharacterMeshTopologyResources& topology,
                                          const CharacterVertexBufferView& vertex_view,
                                          const TriangleGeometryResources& character_geometry,
                                          const TriangleBvhResources& character_bvh,
                                          const std::vector<BvhNodeRange>& node_ranges_by_level,
                                          float collision_thickness) const
{
    return is_initialized() &&
           is_valid_character_mesh_topology_resource(topology) &&
           vertex_view.previous_position_buffer != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0 &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           topology.triangle_count == character_geometry.triangle_count &&
           topology.vertex_count == vertex_view.vertex_count &&
           is_valid_triangle_bvh_resource(character_bvh) &&
           !node_ranges_by_level.empty() &&
           collision_thickness > 0.0f;
}

bool TriangleBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    first_node_location_ = gl.glGetUniformLocation(program_, "uFirstNode");
    node_count_location_ = gl.glGetUniformLocation(program_, "uNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (first_node_location_ < 0 || node_count_location_ < 0 || collision_thickness_location_ < 0) {
        std::cerr << "Character BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    update_timer_.initialize("character triangle BVH bounds update", gpu_timing_log_interval, gl);
    return true;
}

void TriangleBvhBoundsUpdater::update(const CharacterMeshTopologyResources& topology,
                                      const CharacterVertexBufferView& vertex_view,
                                      const TriangleGeometryResources& character_geometry,
                                      const TriangleBvhResources& character_bvh,
                                      const std::vector<BvhNodeRange>& node_ranges_by_level,
                                      float collision_thickness,
                                      QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(topology, vertex_view, character_geometry, character_bvh, node_ranges_by_level, collision_thickness)) {
        return;
    }

    const bool gpu_timing_started = update_timer_.begin(gl);

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_indices_binding, topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_previous_positions_binding, vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_bounds_binding, character_bvh.triangle_bounds_buffer);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness);

    for (const BvhNodeRange& range : node_ranges_by_level) {
        if (range.node_count == 0 ||
            range.first_node >= character_bvh.node_count ||
            range.first_node + range.node_count > character_bvh.node_count) {
            continue;
        }

        gl.glProgramUniform1ui(program_, first_node_location_, range.first_node);
        gl.glProgramUniform1ui(program_, node_count_location_, range.node_count);
        gl.glDispatchCompute(compute_group_count(range.node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    if (gpu_timing_started) {
        update_timer_.end(gl);
    }
}

void TriangleBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    update_timer_.release(gl);
    gl.glDeleteProgram(program_);

    program_ = 0;
    first_node_location_ = -1;
    node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
