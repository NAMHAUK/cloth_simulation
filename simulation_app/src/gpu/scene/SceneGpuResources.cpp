#include "gpu/scene/SceneGpuResources.h"

#include "scene/SceneState.h"

bool SceneGpuResources::is_initialized() const
{
    return initialized_;
}

bool SceneGpuResources::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    if (!normal_updater_.initialize(gl)) {
        return false;
    }

    initialized_ = true;
    return true;
}

void SceneGpuResources::sync(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized()) {
        return;
    }

    const bool character_frame_changed = update_character_frame(scene);
    if (character_frame_changed) {
        normal_updater_.update_normals(character_gpu_state_.mesh_topology_resources(),
                                       character_gpu_state_.mesh_normal_resources(),
                                       gl);
    }

    const bool garments_changed = garment_revision_ != scene.garment_revision();
    if (garments_changed) {
        set_garment_meshes(scene, gl);
    }

    if (!garments_changed && cloth_gpu_state_.is_initialized()) {
        normal_updater_.update_normals(cloth_gpu_state_.mesh_topology_resources(),
                                       cloth_gpu_state_.mesh_normal_resources(),
                                       gl);
    }
}

void SceneGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    normal_updater_.release(gl);

    character_revision_ = 0;
    garment_revision_ = 0;
    current_character_frame_ = 0;
    initialized_ = false;
    character_uploaded_ = false;
}

// Character //

const CharacterGpuResources& SceneGpuResources::character_gpu_state() const
{
    return character_gpu_state_;
}

void SceneGpuResources::set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    if (!scene.has_character()) {
        return;
    }

    const std::uint64_t scene_revision = scene.character_revision();
    const std::uint32_t scene_frame = scene.current_character_frame();

    // 새 character mesh가 들어오면 전체 frame character mesh를 GPU에 올리고 frame 상태 설정
    const CharacterMesh& character_mesh = scene.character_mesh();
    character_gpu_state_.upload_mesh(character_mesh, gl);
    character_gpu_state_.set_current_frame(scene_frame);
    character_revision_ = scene_revision;
    current_character_frame_ = scene_frame;
    character_uploaded_ = true;
    normal_updater_.update_normals(character_gpu_state_.mesh_topology_resources(),
                                   character_gpu_state_.mesh_normal_resources(),
                                   gl);
}

bool SceneGpuResources::update_character_frame(const SceneState& scene)
{
    if (!scene.has_character() || !character_uploaded_) {
        return false;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();

    // character frame 상태 갱신
    if (current_character_frame_ != scene_frame) {
        character_gpu_state_.set_current_frame(scene_frame);
        current_character_frame_ = scene_frame;
        return true;
    }

    return false;
}

// Garments //

const ClothGpuResources& SceneGpuResources::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

void SceneGpuResources::set_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.sync_garments(scene.garments(), gl);
    garment_revision_ = scene.garment_revision();

    if (cloth_gpu_state_.is_initialized()) {
        normal_updater_.update_normals(cloth_gpu_state_.mesh_topology_resources(),
                                       cloth_gpu_state_.mesh_normal_resources(),
                                       gl);
    }
}

void SceneGpuResources::remove_garment_gpu_state(GarmentId, const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.rebuild_compact_buffers(scene.garments(), gl);
    garment_revision_ = scene.garment_revision();

    if (cloth_gpu_state_.is_initialized()) {
        normal_updater_.update_normals(cloth_gpu_state_.mesh_topology_resources(),
                                       cloth_gpu_state_.mesh_normal_resources(),
                                       gl);
    }
}
