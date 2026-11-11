#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>

// Initialization

void SceneGpuState::initialize(const std::filesystem::path& shader_dir,
                               float attachment_surface_offset,
                               float body_detection_distance,
                               const SceneState& scene,
                               QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.initialize(gl);
    character_gpu_state_.initialize(scene, gl);

    pbd_gpu_state_.character_gpu_state().initialize(shader_dir, scene, character_gpu_state_, gl);
    pbd_gpu_state_.initialize(shader_dir, attachment_surface_offset, body_detection_distance, scene, gl);
    buffer_bindings_.bind_character(character_gpu_state_.buffer_set(),
                                    pbd_gpu_state_.character_gpu_state().buffer_set(),
                                    gl);

    set_character_motion(scene, gl);

    initialized_ = true;
}

// Character

void SceneGpuState::set_character_motion(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.character_gpu_state().set_motion(scene.character_motion(), character_gpu_state_, gl);
    pbd_gpu_state_.update_body_bvh_bounds(gl);
    update_character_vertex_normals(gl);
}

void SceneGpuState::update_character_pose(const SceneState& scene, float frame_alpha, QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.character_gpu_state().update_pose(scene.motion_frame_index(), frame_alpha, character_gpu_state_, gl);
    pbd_gpu_state_.update_body_bvh_bounds(gl);
    update_character_vertex_normals(gl);
}

// Garments

void SceneGpuState::rebuild_garment_resources(const SceneState& scene,
                                              QOpenGLFunctions_4_5_Core& gl,
                                              GarmentLayer changed_layer)
{
    pbd_gpu_state_.cloth_gpu_state().release_distance_constraint_buffers(gl);
    pbd_gpu_state_.cloth_gpu_state().release_collision_buffers(gl);
    release_collision_buffers(gl);

    const auto triangle_vertex_indices = cloth_gpu_state_.rebuild_buffers(scene.garments(), changed_layer, gl);
    pbd_gpu_state_.cloth_gpu_state().rebuild_normal_buffers(triangle_vertex_indices,
                                                            cloth_gpu_state_.element_counts().vertex,
                                                            gl);
    buffer_bindings_.bind_cloth(cloth_gpu_state_.buffer_set(), pbd_gpu_state_.cloth_gpu_state().buffer_set(), gl);

    update_cloth_normals(gl);
}

void SceneGpuState::create_simulation_buffers(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    const auto triangle_vertex_indices = cloth_gpu_state_.upload_triangle_indices(scene.garments(), gl);

    pbd_gpu_state_.cloth_gpu_state().rebuild_normal_buffers(triangle_vertex_indices,
                                                            cloth_gpu_state_.element_counts().vertex,
                                                            gl);
    pbd_gpu_state_.cloth_gpu_state().create_collision_buffers(scene.garments(), cloth_gpu_state_, gl);
    pbd_gpu_state_.cloth_gpu_state().create_distance_constraint_buffers(scene.garments(), cloth_gpu_state_, gl);

    release_collision_buffers(gl);
    pbd_gpu_state_.create_collision_buffers(cloth_gpu_state_.element_counts(), gl);
    buffer_bindings_.bind_collision(pbd_gpu_state_.collision_buffers(), gl);

    if (!pbd_gpu_state_.cloth_gpu_state().has_gpu_objects()) {
        throw std::runtime_error("Cannot prepare garment simulation: a cloth GPU object is missing.");
    }

    buffer_bindings_.bind_cloth(cloth_gpu_state_.buffer_set(), pbd_gpu_state_.cloth_gpu_state().buffer_set(), gl);

    update_cloth_normals(gl);
}

void SceneGpuState::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.upload_garment_placement(garment, gl);
}

void SceneGpuState::initialize_garment_attachments(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    PbdClothGpuState& pbd_cloth_state = pbd_gpu_state_.cloth_gpu_state();
    pbd_cloth_state.upload_attachment_indices(garment, cloth_gpu_state_, gl);
    pbd_gpu_state_.build_attachment_targets(pbd_cloth_state.attachment_states()[garment.layer], gl);
    pbd_cloth_state.activate_attachment_targets(garment.layer);
}

void SceneGpuState::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized() || (!pbd_gpu_state_.cloth_gpu_state().is_initialized())) {
        return;
    }

    cloth_gpu_state_.capture_base_positions(gl);
}

void SceneGpuState::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized() || (!pbd_gpu_state_.cloth_gpu_state().is_initialized())) {
        return;
    }

    cloth_gpu_state_.restore_base_positions(gl);
    pbd_gpu_state_.cloth_gpu_state().reset_velocity(cloth_gpu_state_, 0, cloth_gpu_state_.element_counts().vertex, gl);

    update_cloth_normals(gl);
}

void SceneGpuState::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.clear_base_positions(gl);
}

void SceneGpuState::update_cloth_bvh_bounds(float bounds_margin, QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.update_cloth_bvh_bounds(bounds_margin, gl);
}

void SceneGpuState::update_cloth_normals(QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.update_cloth_normals(cloth_gpu_state_.element_counts(), gl);
}

void SceneGpuState::update_character_vertex_normals(QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.update_character_vertex_normals(character_gpu_state_.vertex_count(), gl);
}

// Accessors

bool SceneGpuState::is_initialized() const
{
    return initialized_;
}

const CharacterGpuState& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

const ClothGpuState& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

PbdGpuState& SceneGpuState::pbd_gpu_state()
{
    return pbd_gpu_state_;
}

const PbdGpuState& SceneGpuState::pbd_gpu_state() const
{
    return pbd_gpu_state_;
}

const CollisionBuffers& SceneGpuState::collision_buffers() const
{
    return pbd_gpu_state_.collision_buffers();
}

// Release

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_garment_resources(gl);
    buffer_bindings_.reset_character_bindings(gl);

    pbd_gpu_state_.release(gl);

    character_gpu_state_.release(gl);
    buffer_bindings_.release(gl);

    initialized_ = false;
}

void SceneGpuState::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.reset_cloth_bindings(gl);

    release_collision_buffers(gl);
    pbd_gpu_state_.cloth_gpu_state().release(gl);

    cloth_gpu_state_.release(gl);
}

void SceneGpuState::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.reset_collision_bindings(gl);
    pbd_gpu_state_.release_collision_buffers(gl);
}
