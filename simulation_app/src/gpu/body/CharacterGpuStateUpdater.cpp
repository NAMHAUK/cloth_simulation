#include "gpu/body/CharacterGpuStateUpdater.h"

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/body/CharacterGpuResources.h"
#include "gpu/body/bvh/EdgeBvhBoundsUpdater.h"
#include "gpu/body/bvh/TriangleBvhBoundsUpdater.h"
#include "gpu/body/bvh/VertexBvhBoundsUpdater.h"
#include "gpu/scene/NormalUpdater.h"
#include "scene/SceneState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cstddef>
#include <iostream>

namespace {
constexpr GLuint animation_positions_binding = 0;
constexpr GLuint current_positions_binding = 1;

constexpr GLuint triangle_position_binding = 0;
constexpr GLuint triangle_indices_binding = 1;
constexpr GLuint triangle_geometry_binding = 2;

constexpr std::uint32_t position_update_local_size = 128;
constexpr std::uint32_t triangle_geometry_local_size = 128;
constexpr std::size_t position_components_per_vertex = 3;

bool is_valid_position_update_input(const CharacterAnimationBufferView& animation_view,
                                    const CharacterVertexBufferView& vertex_view)
{
    return animation_view.position_buffer != 0 &&
           animation_view.frame_count != 0 &&
           animation_view.vertex_count != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0 &&
           vertex_view.vertex_count == animation_view.vertex_count;
}

bool is_valid_endpoint_copy_input(const CharacterVertexBufferView& vertex_view)
{
    return vertex_view.previous_position_buffer != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0;
}

bool is_valid_triangle_geometry_input(const CharacterMeshTopologyResources& topology,
                                      const CharacterVertexBufferView& vertex_view,
                                      const TriangleGeometryResources& triangle_geometry)
{
    return vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_count != 0 &&
           topology.triangle_index_buffer != 0 &&
           topology.vertex_count != 0 &&
           topology.triangle_count != 0 &&
           topology.vertex_count == vertex_view.vertex_count &&
           is_valid_triangle_geometry_resource(triangle_geometry) &&
           triangle_geometry.triangle_count == topology.triangle_count;
}
}

CharacterGpuStateUpdater::CharacterGpuStateUpdater(CharacterGpuResources& character_gpu_state,
                                                   TriangleBvhBoundsUpdater& bvh_bounds_updater,
                                                   VertexBvhBoundsUpdater& vertex_bvh_bounds_updater,
                                                   EdgeBvhBoundsUpdater& edge_bvh_bounds_updater,
                                                   NormalUpdater& normal_updater)
    : character_gpu_state_(character_gpu_state),
      bvh_bounds_updater_(bvh_bounds_updater),
      vertex_bvh_bounds_updater_(vertex_bvh_bounds_updater),
      edge_bvh_bounds_updater_(edge_bvh_bounds_updater),
      normal_updater_(normal_updater)
{
}

bool CharacterGpuStateUpdater::is_initialized() const
{
    return position_program_ != 0 && triangle_geometry_program_ != 0;
}

bool CharacterGpuStateUpdater::initialize(const std::filesystem::path& position_shader_path,
                                          const std::filesystem::path& triangle_geometry_shader_path,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    position_program_ = load_compute_program(position_shader_path, "Character vertex position update", gl);
    if (position_program_ == 0) {
        return false;
    }

    triangle_geometry_program_ = load_compute_program(triangle_geometry_shader_path, "Character triangle geometry update", gl);
    if (triangle_geometry_program_ == 0) {
        release(gl);
        return false;
    }

    position_current_frame_base_location_ = gl.glGetUniformLocation(position_program_, "uCurrentFrameBase");
    position_next_frame_base_location_ = gl.glGetUniformLocation(position_program_, "uNextFrameBase");
    position_frame_alpha_location_ = gl.glGetUniformLocation(position_program_, "uFrameAlpha");
    position_vertex_count_location_ = gl.glGetUniformLocation(position_program_, "uVertexCount");
    triangle_count_location_ = gl.glGetUniformLocation(triangle_geometry_program_, "uTriangleCount");

    if (position_current_frame_base_location_ < 0 ||
        position_next_frame_base_location_ < 0 ||
        position_frame_alpha_location_ < 0 ||
        position_vertex_count_location_ < 0 ||
        triangle_count_location_ < 0) {
        std::cerr << "Character GPU state update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void CharacterGpuStateUpdater::initialize_character_pose_state(const CharacterFrameInterpolation& interpolation,
                                                              const std::vector<BvhNodeRange>& node_ranges_by_level,
                                                              const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                                              const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                                              float collision_thickness,
                                                              QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    // Initialization writes the selected pose to current, then mirrors it into previous.
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    write_current_position_buffer(interpolation, vertex_view, gl);
    copy_current_position_to_previous(vertex_view, gl);
    update_derived_pose_state(node_ranges_by_level, body_vertex_node_ranges_by_level, body_edge_node_ranges_by_level, collision_thickness, gl);
}

void CharacterGpuStateUpdater::update_character_pose_state(const CharacterFrameInterpolation& interpolation,
                                                          const std::vector<BvhNodeRange>& node_ranges_by_level,
                                                          const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                                          const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                                          float collision_thickness,
                                                          QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    // Continuous update carries old current into previous before writing the new current pose.
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    copy_current_position_to_previous(vertex_view, gl);
    write_current_position_buffer(interpolation, vertex_view, gl);
    update_derived_pose_state(node_ranges_by_level, body_vertex_node_ranges_by_level, body_edge_node_ranges_by_level, collision_thickness, gl);
}

void CharacterGpuStateUpdater::write_current_position_buffer(const CharacterFrameInterpolation& interpolation,
                                                            const CharacterVertexBufferView& vertex_view,
                                                            QOpenGLFunctions_4_5_Core& gl) const
{
    const CharacterAnimationBufferView animation_view = character_gpu_state_.animation_buffer_view();
    if (!is_valid_position_update_input(animation_view, vertex_view)) {
        return;
    }

    const std::uint32_t current_frame_base =
        character_gpu_state_.frame_position_begin_index(interpolation.current_frame_index);
    const std::uint32_t next_frame_base =
        character_gpu_state_.frame_position_begin_index(interpolation.next_frame_index);

    gl.glUseProgram(position_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, animation_positions_binding, animation_view.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, vertex_view.current_position_buffer);
    gl.glProgramUniform1ui(position_program_, position_current_frame_base_location_, current_frame_base);
    gl.glProgramUniform1ui(position_program_, position_next_frame_base_location_, next_frame_base);
    gl.glProgramUniform1f(position_program_, position_frame_alpha_location_, interpolation.frame_alpha);
    gl.glProgramUniform1ui(position_program_, position_vertex_count_location_, vertex_view.vertex_count);

    gl.glDispatchCompute(compute_group_count(vertex_view.vertex_count, position_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CharacterGpuStateUpdater::copy_current_position_to_previous(const CharacterVertexBufferView& vertex_view,
                                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_valid_endpoint_copy_input(vertex_view)) {
        return;
    }

    const GLsizeiptr position_bytes = static_cast<GLsizeiptr>(
        static_cast<std::size_t>(vertex_view.vertex_count) * position_components_per_vertex * sizeof(float)
    );

    gl.glCopyNamedBufferSubData(vertex_view.current_position_buffer, vertex_view.previous_position_buffer, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CharacterGpuStateUpdater::update_triangle_geometry(const CharacterMeshTopologyResources& topology,
                                                       const CharacterVertexBufferView& vertex_view,
                                                       const TriangleGeometryResources& triangle_geometry,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_valid_triangle_geometry_input(topology, vertex_view, triangle_geometry)) {
        return;
    }

    gl.glUseProgram(triangle_geometry_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_position_binding, vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_indices_binding, topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_geometry_binding, triangle_geometry.triangle_geometry_buffer);
    gl.glProgramUniform1ui(triangle_geometry_program_, triangle_count_location_, topology.triangle_count);

    gl.glDispatchCompute(compute_group_count(topology.triangle_count, triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CharacterGpuStateUpdater::update_derived_pose_state(const std::vector<BvhNodeRange>& node_ranges_by_level,
                                                         const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                                         const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                                         float collision_thickness,
                                                         QOpenGLFunctions_4_5_Core& gl) const
{
    const CharacterMeshTopologyResources topology = character_gpu_state_.mesh_topology_resources();
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    const TriangleGeometryResources triangle_geometry = character_gpu_state_.character_triangle_geometry_resources();

    update_triangle_geometry(topology, vertex_view, triangle_geometry, gl);
    bvh_bounds_updater_.update(triangle_geometry,
                               character_gpu_state_.character_bvh_resources(),
                               node_ranges_by_level,
                               collision_thickness,
                               gl);
    vertex_bvh_bounds_updater_.update(vertex_view,
                                      character_gpu_state_.body_vertex_bvh_resources(),
                                      body_vertex_node_ranges_by_level,
                                      collision_thickness,
                                      gl);
    edge_bvh_bounds_updater_.update(vertex_view,
                                    character_gpu_state_.body_edge_bvh_resources(),
                                    body_edge_node_ranges_by_level,
                                    collision_thickness,
                                    gl);
    normal_updater_.update_character_normals(topology, character_gpu_state_.mesh_normal_resources(), gl);
}

void CharacterGpuStateUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(triangle_geometry_program_);
    gl.glDeleteProgram(position_program_);

    position_program_ = 0;
    triangle_geometry_program_ = 0;
    position_current_frame_base_location_ = -1;
    position_next_frame_base_location_ = -1;
    position_frame_alpha_location_ = -1;
    position_vertex_count_location_ = -1;
    triangle_count_location_ = -1;
}
