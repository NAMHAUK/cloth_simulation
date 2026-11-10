#pragma once

#include "gpu/character/CharacterGpuState.h"
#include "gpu/cloth/ClothGpuState.h"

#include <QOpenGLFunctions_4_5_Core>

class SceneState;

class SceneGpuState final
{
public:
    SceneGpuState() = default;
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    void initialize(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    bool is_initialized() const;
    const CharacterGpuState& character_gpu_state() const;
    ClothGpuState& cloth_gpu_state();
    const ClothGpuState& cloth_gpu_state() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    CharacterGpuState character_gpu_state_;
    ClothGpuState cloth_gpu_state_;

    bool initialized_ = false;
};
