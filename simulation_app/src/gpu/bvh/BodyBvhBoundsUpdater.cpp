#include "gpu/bvh/BodyBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <stdexcept>

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

bool is_valid_level_state(const BvhLevelState& level_state, std::uint32_t total_node_count)
{
    return level_state.node_count != 0 &&
           level_state.node_start_index < total_node_count &&
           level_state.node_start_index + level_state.node_count <= total_node_count;
}

BvhLevelState valid_or_empty_level_state(const std::vector<BvhLevelState>& levels,
                                         std::size_t level_index,
                                         std::uint32_t total_node_count)
{
    if (level_index >= levels.size()) {
        return {};
    }

    const BvhLevelState level_state = levels[level_index];
    return is_valid_level_state(level_state, total_node_count) ? level_state : BvhLevelState{};
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
                                      const std::vector<BvhLevelState>& triangle_levels,
                                      const std::vector<BvhLevelState>& vertex_levels,
                                      const std::vector<BvhLevelState>& edge_levels,
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
           !triangle_levels.empty() &&
           !vertex_levels.empty() &&
           !edge_levels.empty() &&
           collision_thickness > 0.0f;
}

void BodyBvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "body" / "body_bvh_bounds_update.comp",
                                    "Body BVH bounds update",
                                    gl);
    triangle_node_start_index_location_ = gl.glGetUniformLocation(program_, "uTriangleNodeStartIndex");
    triangle_node_count_location_ = gl.glGetUniformLocation(program_, "uTriangleNodeCount");
    vertex_node_start_index_location_ = gl.glGetUniformLocation(program_, "uVertexNodeStartIndex");
    vertex_node_count_location_ = gl.glGetUniformLocation(program_, "uVertexNodeCount");
    edge_node_start_index_location_ = gl.glGetUniformLocation(program_, "uEdgeNodeStartIndex");
    edge_node_count_location_ = gl.glGetUniformLocation(program_, "uEdgeNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (triangle_node_start_index_location_ < 0 ||
        triangle_node_count_location_ < 0 ||
        vertex_node_start_index_location_ < 0 ||
        vertex_node_count_location_ < 0 ||
        edge_node_start_index_location_ < 0 ||
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
                                  const std::vector<BvhLevelState>& triangle_levels,
                                  const std::vector<BvhLevelState>& vertex_levels,
                                  const std::vector<BvhLevelState>& edge_levels,
                                  float collision_thickness,
                                  QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(topology,
                    vertex_view,
                    body_triangle_geometry,
                    body_triangle_bvh,
                    body_vertex_bvh,
                    body_edge_bvh,
                    triangle_levels,
                    vertex_levels,
                    edge_levels,
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
        std::max({triangle_levels.size(), vertex_levels.size(), edge_levels.size()});
    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        const BvhLevelState triangle_level_state =
            valid_or_empty_level_state(triangle_levels, level_index, body_triangle_bvh.node_count);
        const BvhLevelState vertex_level_state =
            valid_or_empty_level_state(vertex_levels, level_index, body_vertex_bvh.node_count);
        const BvhLevelState edge_level_state =
            valid_or_empty_level_state(edge_levels, level_index, body_edge_bvh.node_count);

        const std::uint32_t dispatch_node_count = std::max(
            {triangle_level_state.node_count, vertex_level_state.node_count, edge_level_state.node_count});
        if (dispatch_node_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_,
                               triangle_node_start_index_location_,
                               triangle_level_state.node_start_index);
        gl.glProgramUniform1ui(program_, triangle_node_count_location_, triangle_level_state.node_count);
        gl.glProgramUniform1ui(program_,
                               vertex_node_start_index_location_,
                               vertex_level_state.node_start_index);
        gl.glProgramUniform1ui(program_, vertex_node_count_location_, vertex_level_state.node_count);
        gl.glProgramUniform1ui(program_, edge_node_start_index_location_, edge_level_state.node_start_index);
        gl.glProgramUniform1ui(program_, edge_node_count_location_, edge_level_state.node_count);
        gl.glDispatchCompute(compute_group_count(dispatch_node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void BodyBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    triangle_node_start_index_location_ = -1;
    triangle_node_count_location_ = -1;
    vertex_node_start_index_location_ = -1;
    vertex_node_count_location_ = -1;
    edge_node_start_index_location_ = -1;
    edge_node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
