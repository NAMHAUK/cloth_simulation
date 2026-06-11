#include "gpu/scene/SceneGpuState.h"

#include "app/ProjectPaths.h"
#include "scene/SceneState.h"

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
    character_gpu_state_.upload_mesh(character_mesh, gl);
    character_gpu_state_.set_current_frame(0);
    update_character_triangle_geometry(gl);
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

    if (character_gpu_state_.current_frame_index() == scene_frame) {
        return;
    }

    character_gpu_state_.set_current_frame(scene_frame);
    update_character_triangle_geometry(gl);
}

void SceneGpuState::update_character_triangle_geometry(QOpenGLFunctions_4_5_Core& gl)
{
    triangle_geometry_updater_.update(character_gpu_state_.mesh_topology_resources(),
                                      character_gpu_state_.character_triangle_geometry_resources(),
                                      gl);
    bvh_bounds_updater_.update(character_gpu_state_.character_triangle_geometry_resources(),
                               character_gpu_state_.character_bvh_resources(),
                               character_gpu_state_.bvh_node_ranges_by_level(),
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

void SceneGpuState::remove_garment_gpu_state(std::uint32_t, const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.update_garment_buffers(scene.garments(), gl);
    normal_updater_.update_cloth_normals(cloth_gpu_state_.mesh_topology_resources(),
                                         cloth_gpu_state_.mesh_normal_resources(),
                                         gl);
}
