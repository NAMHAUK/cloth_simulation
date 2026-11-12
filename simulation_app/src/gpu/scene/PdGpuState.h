#pragma once

#include "gpu/cloth/PdClothGpuState.h"

#include <cstdint>

class ClothGpuState;

class PdGpuState final
{
public:
    PdGpuState() = default;
    PdGpuState(const PdGpuState&) = delete;
    PdGpuState& operator=(const PdGpuState&) = delete;

    void rebuild_buffers(const ClothGpuState& cloth_state);
    PdClothGpuState& cloth_gpu_state();
    const PdClothGpuState& cloth_gpu_state() const;

    void release();

private:
    PdClothGpuState cloth_gpu_state_;
};
