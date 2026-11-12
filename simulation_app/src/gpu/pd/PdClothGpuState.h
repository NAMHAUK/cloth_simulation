#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;
struct cudaGraphicsResource;

class PdClothGpuState final
{
public:
    PdClothGpuState() = default;
    PdClothGpuState(const PdClothGpuState&) = delete;
    PdClothGpuState& operator=(const PdClothGpuState&) = delete;

    void rebuild_buffers(const ClothGpuState& cloth_state);
    void initialize_masses(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void reset_velocities() const;

    float4* map_shared_buffers();
    void unmap_shared_buffers();
    float4* velocity_buffer() const;
    const float* mass_buffer() const;
    std::uint32_t vertex_count() const;

    void release();

private:
    cudaGraphicsResource* position_resource_ = nullptr;
    float4* velocity_buffer_ = nullptr;
    float* mass_buffer_ = nullptr;
    std::uint32_t vertex_count_ = 0;
};
