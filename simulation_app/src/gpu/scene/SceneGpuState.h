#pragma once

#include "gpu/body/CharacterGpuResources.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/NormalUpdater.h"
#include "scene/SceneState.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState final {
public:
    SceneGpuState() = default;
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void sync_character_frame(const SceneState& scene);
    void update_mesh_normals(QOpenGLFunctions_4_5_Core& gl);

    const CharacterGpuResources& character_gpu_state() const;
    void set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuResources& cloth_gpu_state() const;
    void set_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void remove_garment_gpu_state(GarmentId garment_id, const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);

private:
    CharacterGpuResources character_gpu_state_;
    ClothGpuResources cloth_gpu_state_;
    NormalUpdater normal_updater_;

    std::uint32_t current_character_frame_ = 0;
    bool initialized_ = false;
    bool character_uploaded_ = false;
};
