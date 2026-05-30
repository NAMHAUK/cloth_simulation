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

    initialized_ = true;
    return true;
}

void SceneGpuState::sync_character_frame(const SceneState& scene)
{
    if (!is_initialized() || !scene.has_character() || !character_uploaded_) {
        return;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();

    if (current_character_frame_ != scene_frame) {
        character_gpu_state_.set_current_frame(scene_frame);
        current_character_frame_ = scene_frame;
    }
}

void SceneGpuState::update_mesh_normals(QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    normal_updater_.update_normals(character_gpu_state_.mesh_topology_resources(),
                                   character_gpu_state_.mesh_normal_resources(),
                                   gl);
    normal_updater_.update_normals(cloth_gpu_state_.mesh_topology_resources(),
                                   cloth_gpu_state_.mesh_normal_resources(),
                                   gl);
}

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    normal_updater_.release(gl);

    current_character_frame_ = 0;
    initialized_ = false;
    character_uploaded_ = false;
}

// Character //

const CharacterGpuResources& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

void SceneGpuState::set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!scene.has_character()) {
        return;
    }

    // 새 character mesh가 들어오면 전체 frame character mesh를 GPU에 올리고 frame 상태 설정
    const CharacterMesh& character_mesh = scene.character_mesh();
    character_gpu_state_.upload_mesh(character_mesh, gl);
    character_gpu_state_.set_current_frame(0);
    current_character_frame_ = 0;
    character_uploaded_ = true;
    update_mesh_normals(gl);
}

// Garments //

const ClothGpuResources& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

void SceneGpuState::set_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.sync_garments(scene.garments(), gl);

    if (cloth_gpu_state_.is_initialized()) {
        update_mesh_normals(gl);
    }
}

void SceneGpuState::remove_garment_gpu_state(GarmentId, const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.rebuild_compact_buffers(scene.garments(), gl);

    if (cloth_gpu_state_.is_initialized()) {
        update_mesh_normals(gl);
    }
}
