#include "gpu/body/bvh/VertexBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint body_current_positions_binding = 0;
constexpr GLuint body_previous_positions_binding = 1;
constexpr GLuint body_vertex_ids_binding = 2;
constexpr GLuint body_vertex_bvh_nodes_binding = 3;
constexpr std::uint32_t vertex_bvh_bounds_update_local_size = 128;
}

bool VertexBvhBoundsUpdater::is_initialized() const
{
    return program_ != 0;
}

bool VertexBvhBoundsUpdater::can_update(const CharacterVertexBufferView& vertex_view,
                                        const VertexBvhResources& body_vertex_bvh,
                                        const std::vector<BvhNodeRange>& node_ranges_by_level,
                                        float collision_thickness) const
{
    return is_initialized() &&
           vertex_view.previous_position_buffer != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0 &&
           is_valid_vertex_bvh_resource(body_vertex_bvh) &&
           !node_ranges_by_level.empty() &&
           collision_thickness > 0.0f;
}

bool VertexBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character vertex BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    first_node_location_ = gl.glGetUniformLocation(program_, "uFirstNode");
    node_count_location_ = gl.glGetUniformLocation(program_, "uNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (first_node_location_ < 0 || node_count_location_ < 0 || collision_thickness_location_ < 0) {
        std::cerr << "Character vertex BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void VertexBvhBoundsUpdater::update(const CharacterVertexBufferView& vertex_view,
                                    const VertexBvhResources& body_vertex_bvh,
                                    const std::vector<BvhNodeRange>& node_ranges_by_level,
                                    float collision_thickness,
                                    QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(vertex_view, body_vertex_bvh, node_ranges_by_level, collision_thickness)) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_current_positions_binding, vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_previous_positions_binding, vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_ids_binding, body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_bvh_nodes_binding, body_vertex_bvh.node_buffer);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness);

    for (const BvhNodeRange& range : node_ranges_by_level) {
        if (range.node_count == 0 ||
            range.first_node >= body_vertex_bvh.node_count ||
            range.first_node + range.node_count > body_vertex_bvh.node_count) {
            continue;
        }

        gl.glProgramUniform1ui(program_, first_node_location_, range.first_node);
        gl.glProgramUniform1ui(program_, node_count_location_, range.node_count);
        gl.glDispatchCompute(compute_group_count(range.node_count, vertex_bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void VertexBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    first_node_location_ = -1;
    node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
