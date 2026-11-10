#include "gpu/scene/PdGpuState.h"

void PdGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    cloth_gpu_state_.rebuild_buffers(cloth_state);
}

PdClothGpuState& PdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PdClothGpuState& PdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

void PdGpuState::release()
{
    cloth_gpu_state_.release();
}
