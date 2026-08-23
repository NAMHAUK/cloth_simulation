#include "gpu/character/CharacterGpuStateUpdater.h"

#include "gpu/bvh/BodyBvhBoundsUpdater.h"
#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/character/CharacterGpuResources.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cstddef>
#include <stdexcept>

namespace {
constexpr GLuint animation_positions_binding = 0;
constexpr GLuint current_positions_binding = 1;

constexpr GLuint triangle_position_binding = 0;
constexpr GLuint triangle_indices_binding = 1;
constexpr GLuint triangle_geometry_binding = 2;

constexpr std::uint32_t position_update_local_size = 128;
constexpr std::uint32_t triangle_geometry_local_size = 128;
constexpr std::size_t position_components_per_vertex = 4;

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
                                                   BodyBvhBoundsUpdater& bvh_bounds_updater)
    : character_gpu_state_(character_gpu_state),
      bvh_bounds_updater_(bvh_bounds_updater)
{}

bool CharacterGpuStateUpdater::is_initialized() const
{
    return position_program_ != 0 && triangle_geometry_program_ != 0;
}

void CharacterGpuStateUpdater::initialize(const std::filesystem::path& shader_dir,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path character_shader_dir = shader_dir / "character";
    position_program_ = load_compute_program(character_shader_dir / "character_vertex_position_update.comp",
                                             "Character vertex position update",
                                             gl);
    triangle_geometry_program_ =
        load_compute_program(character_shader_dir / "character_triangle_geometry_update.comp",
                             "Character triangle geometry update",
                             gl);
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
        throw std::runtime_error("Character GPU state update compute shader missing required uniforms.");
    }
}

void CharacterGpuStateUpdater::initialize_character_pose_state(
    float frame_alpha,
    const std::vector<std::uint32_t>& body_triangle_level_offsets,
    const std::vector<std::uint32_t>& body_vertex_level_offsets,
    const std::vector<std::uint32_t>& body_edge_level_offsets,
    float detection_distance,
    QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    // Initialization writes the selected pose to current, then mirrors it into previous.
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    write_current_position_buffer(frame_alpha, vertex_view, gl);
    copy_current_position_to_previous(vertex_view, gl);
    update_derived_pose_state(body_triangle_level_offsets,
                              body_vertex_level_offsets,
                              body_edge_level_offsets,
                              detection_distance,
                              gl);
}

void CharacterGpuStateUpdater::update_character_pose_state(
    float frame_alpha,
    const std::vector<std::uint32_t>& body_triangle_level_offsets,
    const std::vector<std::uint32_t>& body_vertex_level_offsets,
    const std::vector<std::uint32_t>& body_edge_level_offsets,
    float detection_distance,
    QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    // Continuous update carries old current into previous before writing the new current pose.
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    copy_current_position_to_previous(vertex_view, gl);
    write_current_position_buffer(frame_alpha, vertex_view, gl);
    update_derived_pose_state(body_triangle_level_offsets,
                              body_vertex_level_offsets,
                              body_edge_level_offsets,
                              detection_distance,
                              gl);
}

void CharacterGpuStateUpdater::write_current_position_buffer(float frame_alpha,
                                                             const CharacterVertexBufferView& vertex_view,
                                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const CharacterAnimationBufferView animation_view = character_gpu_state_.animation_buffer_view();
    if (!is_valid_position_update_input(animation_view, vertex_view)) {
        return;
    }

    const std::uint32_t current_frame_base =
        character_gpu_state_.frame_position_begin_index(character_gpu_state_.current_frame_index());
    const std::uint32_t next_frame_base =
        character_gpu_state_.frame_position_begin_index(character_gpu_state_.next_frame_index());

    gl.glUseProgram(position_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        animation_positions_binding,
                        animation_view.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        vertex_view.current_position_buffer);
    gl.glProgramUniform1ui(position_program_, position_current_frame_base_location_, current_frame_base);
    gl.glProgramUniform1ui(position_program_, position_next_frame_base_location_, next_frame_base);
    gl.glProgramUniform1f(position_program_, position_frame_alpha_location_, frame_alpha);
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

    const GLsizeiptr position_bytes =
        byte_size<float>(vertex_view.vertex_count * position_components_per_vertex);

    gl.glCopyNamedBufferSubData(vertex_view.current_position_buffer,
                                vertex_view.previous_position_buffer,
                                0,
                                0,
                                position_bytes);
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        triangle_position_binding,
                        vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_indices_binding, topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        triangle_geometry_binding,
                        triangle_geometry.triangle_geometry_buffer);
    gl.glProgramUniform1ui(triangle_geometry_program_, triangle_count_location_, topology.triangle_count);

    gl.glDispatchCompute(compute_group_count(topology.triangle_count, triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CharacterGpuStateUpdater::update_derived_pose_state(
    const std::vector<std::uint32_t>& body_triangle_level_offsets,
    const std::vector<std::uint32_t>& body_vertex_level_offsets,
    const std::vector<std::uint32_t>& body_edge_level_offsets,
    float detection_distance,
    QOpenGLFunctions_4_5_Core& gl) const
{
    const CharacterMeshTopologyResources topology = character_gpu_state_.mesh_topology_resources();
    const CharacterVertexBufferView vertex_view = character_gpu_state_.character_vertex_buffer_view();
    const TriangleGeometryResources triangle_geometry =
        character_gpu_state_.character_triangle_geometry_resources();

    update_triangle_geometry(topology, vertex_view, triangle_geometry, gl);
    bvh_bounds_updater_.update(topology,
                               vertex_view,
                               triangle_geometry,
                               character_gpu_state_.body_triangle_bvh_buffer_view(),
                               character_gpu_state_.body_vertex_bvh_buffer_view(),
                               character_gpu_state_.body_edge_bvh_buffer_view(),
                               body_triangle_level_offsets,
                               body_vertex_level_offsets,
                               body_edge_level_offsets,
                               detection_distance,
                               gl);
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
