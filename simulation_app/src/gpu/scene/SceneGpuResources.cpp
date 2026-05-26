#include "gpu/scene/SceneGpuResources.h"

#include "scene/SceneState.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {
constexpr std::uint64_t unuploaded_revision = std::numeric_limits<std::uint64_t>::max();
}

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

    update_character_frame(scene);
    normal_updater_.update_normals(character_gpu_state_.normal_update_inputs(), gl);

    for (GarmentGpuSlot& slot : garment_gpu_slots_) {
        normal_updater_.update_normals(slot.gpu_state.normal_update_inputs(), gl);
    }
}

void SceneGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    for (GarmentGpuSlot& slot : garment_gpu_slots_) {
        slot.gpu_state.release(gl);
    }
    garment_gpu_slots_.clear();

    character_gpu_state_.release(gl);
    normal_updater_.release(gl);

    character_revision_ = 0;
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
    normal_updater_.update_normals(character_gpu_state_.normal_update_inputs(), gl);
}

void SceneGpuResources::update_character_frame(const SceneState& scene)
{
    if (!scene.has_character() || !character_uploaded_) {
        return;
    }

    const std::uint32_t scene_frame = scene.current_character_frame();

    // character frame 상태 갱신
    if (current_character_frame_ != scene_frame) {
        character_gpu_state_.set_current_frame(scene_frame);
        current_character_frame_ = scene_frame;
    }
}

// garment //

const ClothGpuResources* SceneGpuResources::garment_gpu_state(GarmentId garment_id) const
{
    const GarmentGpuSlot* slot = find_garment_gpu_slot(garment_id);
    if (slot == nullptr) {
        return nullptr;
    }

    return &slot->gpu_state;
}

void SceneGpuResources::set_garment_mesh(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    GarmentGpuSlot* slot = find_garment_gpu_slot(garment.id);
    if (slot == nullptr) {
        std::cerr << "Missing garment GPU state for garment id " << garment.id << ".\n";
        return;
    }

    if (slot->uploaded_revision == garment.revision) {
        return;
    }

    slot->gpu_state.upload(garment.mesh, gl);
    slot->uploaded_revision = garment.revision;
    normal_updater_.update_normals(slot->gpu_state.normal_update_inputs(), gl);
}

// 새로운 garment GPU state 추가
void SceneGpuResources::add_garment_gpu_state(GarmentId garment_id)
{
    if (find_garment_gpu_slot(garment_id) != nullptr) {
        return;
    }

    GarmentGpuSlot& new_slot = garment_gpu_slots_.emplace_back();
    new_slot.id = garment_id;
    new_slot.uploaded_revision = unuploaded_revision;
}

void SceneGpuResources::remove_garment_gpu_state(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl)
{
}

// id에 해당하는 garment 위치 찾기
SceneGpuResources::GarmentGpuSlot* SceneGpuResources::find_garment_gpu_slot(GarmentId garment_id)
{
    const auto iter = std::find_if(garment_gpu_slots_.begin(), garment_gpu_slots_.end(),
        [garment_id](const GarmentGpuSlot& slot) {
            return slot.id == garment_id;
        }
    );
    if (iter == garment_gpu_slots_.end()) {
        return nullptr;
    }

    return &(*iter);
}

const SceneGpuResources::GarmentGpuSlot* SceneGpuResources::find_garment_gpu_slot(GarmentId garment_id) const
{
    const auto iter = std::find_if(garment_gpu_slots_.begin(), garment_gpu_slots_.end(),
        [garment_id](const GarmentGpuSlot& slot) {
            return slot.id == garment_id;
        }
    );
    if (iter == garment_gpu_slots_.end()) {
        return nullptr;
    }

    return &(*iter);
}
