#include "gpu/scene/SceneGpuState.h"

#include "app/ProjectPaths.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <iostream>

namespace {
CharacterFrameInterpolation make_single_frame_interpolation(std::uint32_t frame_index)
{
    return {frame_index, frame_index, 0.0f};
}
}

SceneGpuState::SceneGpuState()
    : character_gpu_state_updater_(character_gpu_state_, bvh_bounds_updater_, vertex_bvh_bounds_updater_, normal_updater_)
{
}

bool SceneGpuState::is_initialized() const
{
    return initialized_;
}

bool SceneGpuState::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    if (!normal_updater_.initialize(shader_paths.triangle_normal_compute, shader_paths.vertex_normal_compute, gl)) {
        return false;
    }
    if (!bvh_bounds_updater_.initialize(shader_paths.character_bvh_bounds_update_compute, gl)) {
        normal_updater_.release(gl);
        return false;
    }
    if (!vertex_bvh_bounds_updater_.initialize(shader_paths.character_vertex_bvh_bounds_update_compute, gl)) {
        bvh_bounds_updater_.release(gl);
        normal_updater_.release(gl);
        return false;
    }
    if (!character_gpu_state_updater_.initialize(shader_paths.character_vertex_position_update_compute,
                                                 shader_paths.character_triangle_geometry_update_compute,
                                                 gl)) {
        vertex_bvh_bounds_updater_.release(gl);
        bvh_bounds_updater_.release(gl);
        normal_updater_.release(gl);
        return false;
    }
    if (!attachment_target_builder_.initialize(shader_paths.garment_attachment_target_build_compute, gl)) {
        character_gpu_state_updater_.release(gl);
        vertex_bvh_bounds_updater_.release(gl);
        bvh_bounds_updater_.release(gl);
        normal_updater_.release(gl);
        return false;
    }

    initialized_ = true;
    return true;
}

void SceneGpuState::update_mesh_normals(QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    normal_updater_.update_character_normals(character_gpu_state_.mesh_topology_resources(),
                                             character_gpu_state_.mesh_normal_resources(),
                                             gl);
    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
}

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    collision_workspace_buffers_.release(gl);
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    character_gpu_state_updater_.release(gl);
    normal_updater_.release(gl);
    vertex_bvh_bounds_updater_.release(gl);
    bvh_bounds_updater_.release(gl);
    attachment_target_builder_.release(gl);

    initialized_ = false;
}

// Character //
const CharacterGpuResources& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

void SceneGpuState::set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    // 새 character mesh가 들어오면 전체 frame character mesh를 GPU에 올리고 frame 상태 설정
    const CharacterMesh& character_mesh = scene.character_mesh();
    character_gpu_state_.upload_mesh(character_mesh,
                                     scene.default_character_bvh_data(),
                                     scene.default_body_vertex_bvh_data(),
                                     gl);
    character_gpu_state_.set_current_frame(0);
    character_gpu_state_updater_.initialize_character_pose_state(make_single_frame_interpolation(0),
                                                                 scene.default_character_bvh_data().node_ranges_by_level,
                                                                 scene.default_body_vertex_bvh_data().node_ranges_by_level,
                                                                 simulation_settings::character_collision_thickness,
                                                                 gl);
}

void SceneGpuState::update_character_frame_interpolation(const SceneState& scene,
                                                         const CharacterFrameInterpolation& interpolation,
                                                         QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    character_gpu_state_updater_.update_character_pose_state(interpolation,
                                                             scene.default_character_bvh_data().node_ranges_by_level,
                                                             scene.default_body_vertex_bvh_data().node_ranges_by_level,
                                                             simulation_settings::character_collision_thickness,
                                                             gl);
}

// Garments //

const ClothGpuResources& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

CollisionWorkspaceBufferView SceneGpuState::collision_workspace_buffer_view() const
{
    return collision_workspace_buffers_.view();
}

void SceneGpuState::update_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.update_garment_buffers(scene.garments(), gl);
    if (cloth_gpu_state_.is_initialized()) {
        const ClothMotionBufferView motion_view = cloth_gpu_state_.motion_buffer_view();
        const ClothCollisionStateBufferView collision_view = cloth_gpu_state_.collision_state_buffer_view();
        const ClothMeshTopologyResources topology = cloth_gpu_state_.mesh_topology_resources();
        if (!collision_workspace_buffers_.ensure_capacity(motion_view.vertex_count,
                                                          topology.triangle_count,
                                                          collision_view.max_contacts_per_vertex,
                                                          gl)) {
            std::cerr << "Failed to prepare collision workspace buffers.\n";
        }
    } else {
        collision_workspace_buffers_.release(gl);
    }
    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
}

bool SceneGpuState::update_garment_placement(const GarmentObject& garment,
                                             bool update_rest_lengths,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.update_garment_placement(garment, update_rest_lengths, gl)) {
        return false;
    }

    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
    return true;
}

void SceneGpuState::build_garment_attachment_targets(SceneState& scene,
                                                     std::uint32_t garment_id,
                                                     QOpenGLFunctions_4_5_Core& gl)
{
    GarmentObject* garment = scene.find_garment(garment_id);
    if (garment == nullptr) {
        return;
    }

    ElementRange target_range;
    if (!cloth_gpu_state_.upload_garment_attachment_vertices(*garment, target_range, gl)) {
        std::cerr << "Failed to upload garment attachment vertices.\n";
        return;
    }

    if (target_range.count == 0u) {
        return;
    }

    const ClothMotionBufferView motion_view = cloth_gpu_state_.motion_buffer_view();
    const AttachmentConstraintBufferView attachment_view = cloth_gpu_state_.attachment_constraint_buffer_view();
    const TriangleGeometryResources character_geometry = character_gpu_state_.character_triangle_geometry_resources();
    const TriangleBvhResources character_bvh = character_gpu_state_.character_bvh_resources();
    
    if (!attachment_target_builder_.build(motion_view, attachment_view, target_range, character_geometry, character_bvh, gl)) {
        std::cerr << "Cannot build garment attachment targets because required GPU buffers are missing.\n";
        return;
    }

    if (!cloth_gpu_state_.activate_attachment_targets(target_range)) {
        std::cerr << "Failed to activate garment attachment targets.\n";
    }
}

bool SceneGpuState::save_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    return cloth_gpu_state_.save_base_positions(gl);
}

bool SceneGpuState::restore_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.restore_base_positions(gl)) {
        return false;
    }

    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
    return true;
}

void SceneGpuState::clear_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.clear_base_positions(gl);
}
