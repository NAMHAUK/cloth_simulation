#include "gpu/bvh/BodyBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <iostream>

namespace {
constexpr GLuint body_triangle_geometry_binding = 0;
constexpr GLuint body_triangle_indices_binding = 1;
constexpr GLuint body_current_positions_binding = 2;
constexpr GLuint body_previous_positions_binding = 3;
constexpr GLuint body_triangle_bvh_node_binding = 4;
constexpr GLuint body_triangle_bounds_binding = 5;
constexpr GLuint body_vertex_ids_binding = 6;
constexpr GLuint body_vertex_bvh_nodes_binding = 7;
constexpr GLuint body_vertex_bounds_binding = 8;
constexpr GLuint body_edge_indices_binding = 9;
constexpr GLuint body_edge_bvh_nodes_binding = 10;
constexpr GLuint body_edge_bounds_binding = 11;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;

bool is_valid_node_range(const BvhNodeRange& range, std::uint32_t node_count)
{
    return range.node_count != 0 &&
           range.first_node < node_count &&
           range.first_node + range.node_count <= node_count;
}

BvhNodeRange valid_or_empty_range(const std::vector<BvhNodeRange>& ranges,
                                  std::size_t level_index,
                                  std::uint32_t node_count)
{
    if (level_index >= ranges.size()) {
        return {};
    }

    const BvhNodeRange range = ranges[level_index];
    return is_valid_node_range(range, node_count) ? range : BvhNodeRange{};
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
                                           const std::vector<BvhNodeRange>& triangle_node_ranges_by_level,
                                           const std::vector<BvhNodeRange>& vertex_node_ranges_by_level,
                                           const std::vector<BvhNodeRange>& edge_node_ranges_by_level,
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
           !triangle_node_ranges_by_level.empty() &&
           !vertex_node_ranges_by_level.empty() &&
           !edge_node_ranges_by_level.empty() &&
           collision_thickness > 0.0f;
}

bool BodyBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Body BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    triangle_first_node_location_ = gl.glGetUniformLocation(program_, "uTriangleFirstNode");
    triangle_node_count_location_ = gl.glGetUniformLocation(program_, "uTriangleNodeCount");
    vertex_first_node_location_ = gl.glGetUniformLocation(program_, "uVertexFirstNode");
    vertex_node_count_location_ = gl.glGetUniformLocation(program_, "uVertexNodeCount");
    edge_first_node_location_ = gl.glGetUniformLocation(program_, "uEdgeFirstNode");
    edge_node_count_location_ = gl.glGetUniformLocation(program_, "uEdgeNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (triangle_first_node_location_ < 0 ||
        triangle_node_count_location_ < 0 ||
        vertex_first_node_location_ < 0 ||
        vertex_node_count_location_ < 0 ||
        edge_first_node_location_ < 0 ||
        edge_node_count_location_ < 0 ||
        collision_thickness_location_ < 0) {
        std::cerr << "Body BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void BodyBvhBoundsUpdater::update(const CharacterMeshTopologyResources& topology,
                                       const CharacterVertexBufferView& vertex_view,
                                       const TriangleGeometryResources& body_triangle_geometry,
                                       const TriangleBvhResources& body_triangle_bvh,
                                       const VertexBvhResources& body_vertex_bvh,
                                       const EdgeBvhResources& body_edge_bvh,
                                       const std::vector<BvhNodeRange>& triangle_node_ranges_by_level,
                                       const std::vector<BvhNodeRange>& vertex_node_ranges_by_level,
                                       const std::vector<BvhNodeRange>& edge_node_ranges_by_level,
                                       float collision_thickness,
                                       QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(topology,
                    vertex_view,
                    body_triangle_geometry,
                    body_triangle_bvh,
                    body_vertex_bvh,
                    body_edge_bvh,
                    triangle_node_ranges_by_level,
                    vertex_node_ranges_by_level,
                    edge_node_ranges_by_level,
                    collision_thickness)) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_geometry_binding, body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_indices_binding, topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_current_positions_binding, vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_previous_positions_binding, vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_bvh_node_binding, body_triangle_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_bounds_binding, body_triangle_bvh.triangle_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_ids_binding, body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_bvh_nodes_binding, body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_bounds_binding, body_vertex_bvh.vertex_bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_indices_binding, body_edge_bvh.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_bvh_nodes_binding, body_edge_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_edge_bounds_binding, body_edge_bvh.edge_bounds_buffer);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness);

    const std::size_t level_count = std::max({
        triangle_node_ranges_by_level.size(),
        vertex_node_ranges_by_level.size(),
        edge_node_ranges_by_level.size()
    });
    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        const BvhNodeRange triangle_range =
            valid_or_empty_range(triangle_node_ranges_by_level, level_index, body_triangle_bvh.node_count);
        const BvhNodeRange vertex_range =
            valid_or_empty_range(vertex_node_ranges_by_level, level_index, body_vertex_bvh.node_count);
        const BvhNodeRange edge_range =
            valid_or_empty_range(edge_node_ranges_by_level, level_index, body_edge_bvh.node_count);

        const std::uint32_t node_count = std::max({
            triangle_range.node_count,
            vertex_range.node_count,
            edge_range.node_count
        });
        if (node_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, triangle_first_node_location_, triangle_range.first_node);
        gl.glProgramUniform1ui(program_, triangle_node_count_location_, triangle_range.node_count);
        gl.glProgramUniform1ui(program_, vertex_first_node_location_, vertex_range.first_node);
        gl.glProgramUniform1ui(program_, vertex_node_count_location_, vertex_range.node_count);
        gl.glProgramUniform1ui(program_, edge_first_node_location_, edge_range.first_node);
        gl.glProgramUniform1ui(program_, edge_node_count_location_, edge_range.node_count);
        gl.glDispatchCompute(compute_group_count(node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

}

void BodyBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    triangle_first_node_location_ = -1;
    triangle_node_count_location_ = -1;
    vertex_first_node_location_ = -1;
    vertex_node_count_location_ = -1;
    edge_first_node_location_ = -1;
    edge_node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
