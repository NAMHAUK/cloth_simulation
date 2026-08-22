#include "gpu/scene/SceneGpuState.h"

#include "scene/SceneState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t attachment_target_local_size = 128;
}

SceneGpuState::SceneGpuState()
    : character_gpu_state_updater_(character_gpu_state_, bvh_bounds_updater_, normal_updater_)
{}

// Initialization

void SceneGpuState::initialize(const std::filesystem::path& shader_dir,
                               float attachment_surface_offset,
                               QOpenGLFunctions_4_5_Core& gl)
{
    normal_updater_.initialize(shader_dir, gl);
    bvh_bounds_updater_.initialize(shader_dir, gl);
    character_gpu_state_updater_.initialize(shader_dir, gl);

    initialize_attachment_target_program(shader_dir, attachment_surface_offset, gl);

    initialized_ = true;
}

void SceneGpuState::initialize_attachment_target_program(const std::filesystem::path& shader_dir,
                                                         float surface_offset,
                                                         QOpenGLFunctions_4_5_Core& gl)
{
    attachment_target_program_ =
        load_compute_program(shader_dir / "cloth" / "setup" / "garment_attachment_target_build.comp",
                             "Attachment target build",
                             gl);
    attachment_constraint_offset_location_ =
        gl.glGetUniformLocation(attachment_target_program_, "uConstraintOffset");
    attachment_constraint_count_location_ =
        gl.glGetUniformLocation(attachment_target_program_, "uConstraintCount");
    const GLint attachment_surface_offset_location =
        gl.glGetUniformLocation(attachment_target_program_, "uSurfaceOffset");

    if (attachment_constraint_offset_location_ < 0 ||
        attachment_constraint_count_location_ < 0 ||
        attachment_surface_offset_location < 0) {
        throw std::runtime_error("Attachment target build compute shader missing required uniforms.");
    }

    gl.glProgramUniform1f(attachment_target_program_, attachment_surface_offset_location, surface_offset);
}

// Character

void SceneGpuState::set_character_motion(const SceneState& scene,
                                         float body_detection_distance,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.upload_motion(scene.character_motion(),
                                       scene.default_body_triangle_bvh(),
                                       scene.default_body_vertex_bvh(),
                                       scene.default_body_edge_bvh(),
                                       gl);
    character_gpu_state_.set_current_frame(0);
    character_gpu_state_updater_.initialize_character_pose_state(
        0.0f,
        scene.default_body_triangle_bvh().level_offsets,
        scene.default_body_vertex_bvh().level_offsets,
        scene.default_body_edge_bvh().level_offsets,
        body_detection_distance,
        gl);
}

void SceneGpuState::update_character_pose(const SceneState& scene,
                                          float frame_alpha,
                                          float body_detection_distance,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.set_current_frame(scene.motion_frame_index());
    character_gpu_state_updater_.update_character_pose_state(frame_alpha,
                                                             scene.default_body_triangle_bvh().level_offsets,
                                                             scene.default_body_vertex_bvh().level_offsets,
                                                             scene.default_body_edge_bvh().level_offsets,
                                                             body_detection_distance,
                                                             gl);
}

// Garments

void SceneGpuState::rebuild_garment_resources(const SceneState& scene,
                                              QOpenGLFunctions_4_5_Core& gl,
                                              GarmentLayer changed_layer)
{
    assert(!scene.garments().empty());

    const auto counts = cloth_gpu_state_.rebuild_buffers(scene.garments(), changed_layer, gl);
    collision_candidate_buffers_.ensure_capacity(counts.vertex,
                                                 counts.triangle,
                                                 counts.stretch_constraint,
                                                 static_cast<std::uint32_t>(scene.garments().size()),
                                                 gl);
    update_cloth_normals(gl);
}

void SceneGpuState::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.upload_garment_placement(garment, gl);
}

void SceneGpuState::initialize_garment_attachments(const GarmentObject& garment,
                                                   QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.upload_attachment_indices(garment, gl);
    build_attachment_targets(garment.layer, gl);
    cloth_gpu_state_.activate_attachment_targets(garment.layer);
}

void SceneGpuState::build_attachment_targets(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl)
{
    const SimulationGpuView views = simulation_view();
    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
    const std::uint32_t constraint_count = garment_state.attachment_constraint_count;
    if (constraint_count == 0u) {
        return;
    }

    const std::array<GLuint, 5> buffers{
        views.cloth_motion.current_position_buffer,
        views.attachment_constraints.attachment_index_buffer,
        views.attachment_constraints.barycentric_offset_buffer,
        views.body_triangle_geometry.triangle_geometry_buffer,
        views.body_triangle_bvh.node_buffer,
    };

    gl.glUseProgram(attachment_target_program_);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizei>(buffers.size()), buffers.data());
    gl.glProgramUniform1ui(attachment_target_program_,
                           attachment_constraint_offset_location_,
                           garment_state.attachment_constraint_start_index);
    gl.glProgramUniform1ui(attachment_target_program_,
                           attachment_constraint_count_location_,
                           constraint_count);
                           
    gl.glDispatchCompute(compute_group_count(constraint_count, attachment_target_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void SceneGpuState::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized()) {
        return;
    }
    cloth_gpu_state_.capture_base_positions(gl);
}

void SceneGpuState::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized()) {
        return;
    }
    cloth_gpu_state_.restore_base_positions(gl);
    update_cloth_normals(gl);
}

void SceneGpuState::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.clear_base_positions(gl);
}

void SceneGpuState::update_cloth_normals(QOpenGLFunctions_4_5_Core& gl)
{
    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
}

// Accessors

bool SceneGpuState::is_initialized() const
{
    return initialized_;
}

SimulationGpuView SceneGpuState::simulation_view() const
{
    SimulationGpuView views(cloth_gpu_state_.garment_buffer_states());
    views.cloth_motion = cloth_gpu_state_.motion_buffer_view();
    views.cloth_collision_pushout = cloth_gpu_state_.collision_pushout_buffer_view();
    views.cloth_contact_motion = cloth_gpu_state_.contact_motion_buffer_view();
    views.cloth_body_triangle_indices = cloth_gpu_state_.body_triangle_index_buffer_view();
    views.cloth_topology = cloth_gpu_state_.mesh_topology_resources();
    views.cloth_bvh = cloth_gpu_state_.cloth_bvh_buffer_view();
    views.stretch_constraints = cloth_gpu_state_.stretch_constraint_buffer_view();
    views.bending_constraints = cloth_gpu_state_.bending_constraint_buffer_view();
    views.attachment_constraints = cloth_gpu_state_.attachment_constraint_buffer_view();
    views.body_topology = character_gpu_state_.mesh_topology_resources();
    views.body_vertices = character_gpu_state_.character_vertex_buffer_view();
    views.body_triangle_geometry = character_gpu_state_.character_triangle_geometry_resources();
    views.body_triangle_bvh = character_gpu_state_.body_triangle_bvh_buffer_view();
    views.body_vertex_bvh = character_gpu_state_.body_vertex_bvh_buffer_view();
    views.body_edge_bvh = character_gpu_state_.body_edge_bvh_buffer_view();
    views.collision_candidates = collision_candidate_buffers_.view();
    return views;
}

const CharacterGpuResources& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

const ClothGpuResources& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

// Release

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_garment_resources(gl);
    character_gpu_state_.release(gl);
    character_gpu_state_updater_.release(gl);
    normal_updater_.release(gl);
    bvh_bounds_updater_.release(gl);
    gl.glDeleteProgram(attachment_target_program_);

    attachment_target_program_ = 0;
    initialized_ = false;
}

void SceneGpuState::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    collision_candidate_buffers_.release(gl);
    cloth_gpu_state_.release(gl);
}
