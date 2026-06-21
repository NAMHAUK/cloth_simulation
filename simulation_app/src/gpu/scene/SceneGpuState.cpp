#include "gpu/scene/SceneGpuState.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/AttachmentBuilder.h"
#include "scene/SceneState.h"

#include <iostream>

namespace {
CharacterFrameInterpolation make_single_frame_interpolation(std::uint32_t frame_index)
{
    return {frame_index, frame_index, 0.0f};
}
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
    if (!triangle_geometry_updater_.initialize(shader_paths.character_triangle_geometry_update_compute, gl)) {
        normal_updater_.release(gl);
        return false;
    }
    if (!bvh_bounds_updater_.initialize(shader_paths.character_bvh_bounds_update_compute, gl)) {
        triangle_geometry_updater_.release(gl);
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
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    normal_updater_.release(gl);
    triangle_geometry_updater_.release(gl);
    bvh_bounds_updater_.release(gl);

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
    character_gpu_state_.upload_mesh(character_mesh, scene.default_character_bvh_data(), gl);
    character_gpu_state_.set_current_frame(0);
    update_character_triangle_geometry(make_single_frame_interpolation(0), gl);
    update_character_bvh_bounds(scene, gl);
    normal_updater_.update_character_normals(character_gpu_state_.mesh_topology_resources(),
                                             character_gpu_state_.mesh_normal_resources(),
                                             gl);
}

void SceneGpuState::update_character_frame(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();

    character_gpu_state_.set_current_frame(scene_frame);
    update_character_triangle_geometry(make_single_frame_interpolation(scene_frame), gl);
    update_character_bvh_bounds(scene, gl);
}

void SceneGpuState::update_character_frame_interpolation(const SceneState& scene,
                                                         const CharacterFrameInterpolation& interpolation,
                                                         QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    update_character_triangle_geometry(interpolation, gl);
    update_character_bvh_bounds(scene, gl);
}

void SceneGpuState::update_character_render_frame(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !character_gpu_state_.is_initialized()) {
        return;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();
    character_gpu_state_.set_current_frame(scene_frame);
    update_character_triangle_geometry(make_single_frame_interpolation(scene_frame), gl);
}

void SceneGpuState::update_character_triangle_geometry(const CharacterFrameInterpolation& interpolation,
                                                       QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t current_frame_position_begin_index =
        character_gpu_state_.frame_position_begin_index(interpolation.current_frame_index);
    const std::uint32_t next_frame_position_begin_index =
        character_gpu_state_.frame_position_begin_index(interpolation.next_frame_index);

    triangle_geometry_updater_.update(character_gpu_state_.mesh_topology_resources(),
                                      character_gpu_state_.character_triangle_geometry_resources(),
                                      current_frame_position_begin_index,
                                      next_frame_position_begin_index,
                                      interpolation.frame_alpha,
                                      gl);
}

void SceneGpuState::update_character_bvh_bounds(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    bvh_bounds_updater_.update(character_gpu_state_.character_triangle_geometry_resources(),
                               character_gpu_state_.character_bvh_resources(),
                               scene.default_character_bvh_data().node_ranges_by_level,
                               gl);
}

// Garments //

const ClothGpuResources& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

void SceneGpuState::update_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.update_garment_buffers(scene.garments(), gl);
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

    garment->attachment_constraints = attachment_builder::build_garment_attachment_targets(
        *garment,
        scene.character_mesh(),
        scene.current_character_frame(),
        scene.default_character_bvh_data().triangle_indices,
        scene.default_character_bvh_data().nodes
    );

    if (!cloth_gpu_state_.update_garment_attachment_targets(*garment, gl)) {
        std::cerr << "Failed to upload garment attachment targets.\n";
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
