#pragma once

#include <cstdint>

#include <vector_types.h>

class ClothGpuState;
struct cudaGraphicsResource;

class PdClothGpuState final
{
public:
    PdClothGpuState() = default;
    PdClothGpuState(const PdClothGpuState&) = delete;
    PdClothGpuState& operator=(const PdClothGpuState&) = delete;

    void rebuild_buffers(const ClothGpuState& cloth_state);
    void reset_velocities() const;

    float4* map_shared_buffers() const;
    void unmap_shared_buffers() const;
    float4* velocities() const;
    std::uint32_t vertex_count() const;

    void release();

private:
    cudaGraphicsResource* positions_ = nullptr;
    float4* velocities_ = nullptr;
    std::uint32_t vertex_count_ = 0;
};
