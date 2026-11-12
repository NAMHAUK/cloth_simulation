#include "gpu/common/SceneGpuState.h"
#include "simulation/SceneState.h"

// Initialization

void SceneGpuState::initialize(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.initialize(scene, gl);
    initialized_ = true;
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

ClothGpuState& SceneGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const ClothGpuState& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

// Release

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    initialized_ = false;
}
