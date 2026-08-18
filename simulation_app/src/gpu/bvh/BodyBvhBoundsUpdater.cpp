#include "gpu/bvh/BodyBvhBoundsUpdater.h"

#include "gpu/bvh/BvhBuildUtils.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
constexpr GLuint body_triangle_geometry_binding = 0;
constexpr GLuint body_triangle_indices_binding = 1;
constexpr GLuint body_current_positions_binding = 2;
constexpr GLuint body_previous_positions_binding = 3;
constexpr GLuint body_triangle_bvh_node_binding = 4;
constexpr GLuint body_triangle_bounds_binding = 5;
constexpr GLuint body_vertex_indices_binding = 6;
constexpr GLuint body_vertex_bvh_nodes_binding = 7;
constexpr GLuint body_vertex_bounds_binding = 8;
constexpr GLuint body_edge_indices_binding = 9;
constexpr GLuint body_edge_bvh_nodes_binding = 10;
constexpr GLuint body_edge_bounds_binding = 11;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;

std::pair<std::uint32_t, std::uint32_t> valid_or_empty_level(const std::vector<std::uint32_t>& level_offsets,
                                                             std::size_t level_index,
                                                             std::uint32_t total_node_count)
{
    if (level_index + 1u >= level_offsets.size()) {
        return {};
    }

    const std::size_t offset_index = level_offsets.size() - 1u - level_index;
    const std::uint32_t first_node_index = level_offsets[offset_index - 1u];
    const std::uint32_t node_end_index = level_offsets[offset_index];
    if (first_node_index >= node_end_index || node_end_index > total_node_count) {
        return {};
    }
    return {first_node_index, node_end_index - first_node_index};
}
}

bool BodyBvhBoundsUpdater::is_initialized() const
{
    return program_ != 0;
}

bool BodyBvhBoundsUpdater::can_update(const CharacterMeshTopologyResources& topology,
                                      const CharacterVertexBufferView& vertex_view,
                                      const TriangleGeometryResources& body_triangle_geometry,
                                      const TriangleBvhResources& body_triangle_bvh,
                                      const VertexBvhResources& body_vertex_bvh,
                                      const EdgeBvhResources& body_edge_bvh,
                                      const std::vector<std::uint32_t>& triangle_level_offsets,
                                      const std::vector<std::uint32_t>& vertex_level_offsets,
                                      const std::vector<std::uint32_t>& edge_level_offsets,
                                      float collision_thickness) const
{
    return is_initialized() &&
           is_valid_character_mesh_topology_resource(topology) &&
           vertex_view.previous_position_buffer != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0 &&
           is_valid_triangle_geometry_resource(body_triangle_geometry) &&
           topology.triangle_count == body_triangle_geometry.triangle_count &&
           body_triangle_bvh.triangle_count <= topology.triangle_count &&
           topology.vertex_count == vertex_view.vertex_count &&
           is_valid_triangle_bvh_resource(body_triangle_bvh) &&
           is_valid_vertex_bvh_resource(body_vertex_bvh) &&
           is_valid_edge_bvh_resource(body_edge_bvh) &&
           bvh_build::has_valid_bvh_level_offsets(triangle_level_offsets, body_triangle_bvh.node_count) &&
           bvh_build::has_valid_bvh_level_offsets(vertex_level_offsets, body_vertex_bvh.node_count) &&
           bvh_build::has_valid_bvh_level_offsets(edge_level_offsets, body_edge_bvh.node_count) &&
           collision_thickness > 0.0f;
}

void BodyBvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "body" / "body_bvh_bounds_update.comp",
                                    "Body BVH bounds update",
                                    gl);
    triangle_first_node_index_location_ = gl.glGetUniformLocation(program_, "uTriangleFirstNodeIndex");
    triangle_node_count_location_ = gl.glGetUniformLocation(program_, "uTriangleNodeCount");
    vertex_first_node_index_location_ = gl.glGetUniformLocation(program_, "uVertexFirstNodeIndex");
    vertex_node_count_location_ = gl.glGetUniformLocation(program_, "uVertexNodeCount");
    edge_first_node_index_location_ = gl.glGetUniformLocation(program_, "uEdgeFirstNodeIndex");
    edge_node_count_location_ = gl.glGetUniformLocation(program_, "uEdgeNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (triangle_first_node_index_location_ < 0 ||
        triangle_node_count_location_ < 0 ||
        vertex_first_node_index_location_ < 0 ||
        vertex_node_count_location_ < 0 ||
        edge_first_node_index_location_ < 0 ||
        edge_node_count_location_ < 0 ||
        collision_thickness_location_ < 0) {
        throw std::runtime_error("Body BVH bounds update compute shader missing required uniforms.");
    }
}

void BodyBvhBoundsUpdater::update(const CharacterMeshTopologyResources& topology,
                                  const CharacterVertexBufferView& vertex_view,
                                  const TriangleGeometryResources& body_triangle_geometry,
                                  const TriangleBvhResources& body_triangle_bvh,
                                  const VertexBvhResources& body_vertex_bvh,
                                  const EdgeBvhResources& body_edge_bvh,
                                  const std::vector<std::uint32_t>& triangle_level_offsets,
                                  const std::vector<std::uint32_t>& vertex_level_offsets,
                                  const std::vector<std::uint32_t>& edge_level_offsets,
                                  float collision_thickness,
                                  QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(topology,
                    vertex_view,
                    body_triangle_geometry,
                    body_triangle_bvh,
                    body_vertex_bvh,
                    body_edge_bvh,
                    triangle_level_offsets,
                    vertex_level_offsets,
                    edge_level_offsets,
                    collision_thickness)) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_geometry_binding,
                        body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_indices_binding,
                        topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_current_positions_binding,
                        vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_previous_positions_binding,
                        vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_bvh_node_binding,
                        body_triangle_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_bounds_binding,
                        body_triangle_bvh.triangle_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_vertex_indices_binding,
                        body_vertex_bvh.vertex_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_bvh_nodes_binding, body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_vertex_bounds_binding,
                        body_vertex_bvh.vertex_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_indices_binding, body_edge_bvh.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_bvh_nodes_binding, body_edge_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_bounds_binding, body_edge_bvh.edge_bounds_buffer);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness);

    const std::size_t level_count =
        std::max({triangle_level_offsets.size(), vertex_level_offsets.size(), edge_level_offsets.size()}) -
        1u;
    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        const auto [triangle_first_node_index, triangle_node_count] =
            valid_or_empty_level(triangle_level_offsets, level_index, body_triangle_bvh.node_count);
        const auto [vertex_first_node_index, vertex_node_count] =
            valid_or_empty_level(vertex_level_offsets, level_index, body_vertex_bvh.node_count);
        const auto [edge_first_node_index, edge_node_count] =
            valid_or_empty_level(edge_level_offsets, level_index, body_edge_bvh.node_count);

        const std::uint32_t dispatch_node_count =
            std::max({triangle_node_count, vertex_node_count, edge_node_count});
        if (dispatch_node_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, triangle_first_node_index_location_, triangle_first_node_index);
        gl.glProgramUniform1ui(program_, triangle_node_count_location_, triangle_node_count);
        gl.glProgramUniform1ui(program_, vertex_first_node_index_location_, vertex_first_node_index);
        gl.glProgramUniform1ui(program_, vertex_node_count_location_, vertex_node_count);
        gl.glProgramUniform1ui(program_, edge_first_node_index_location_, edge_first_node_index);
        gl.glProgramUniform1ui(program_, edge_node_count_location_, edge_node_count);
        gl.glDispatchCompute(compute_group_count(dispatch_node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void BodyBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    triangle_first_node_index_location_ = -1;
    triangle_node_count_location_ = -1;
    vertex_first_node_index_location_ = -1;
    vertex_node_count_location_ = -1;
    edge_first_node_index_location_ = -1;
    edge_node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
