#pragma once

#include "gpu/pd/CudaMeshResources.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;

class PdClothGpuState final
{
public:
    PdClothGpuState() = default;
    PdClothGpuState(const PdClothGpuState&) = delete;
    PdClothGpuState& operator=(const PdClothGpuState&) = delete;

    void create_simulation_buffers(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void prepare_normal_buffers(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void reset_velocities() const;

    float4* previous_position_buffer() const;
    float4* velocity_buffer() const;
    const float* mass_buffer() const;
    const std::vector<std::uint32_t>& triangle_color_counts() const;
    std::uint32_t vertex_count() const;
    std::uint32_t triangle_count() const;

    void release_simulation_buffers();
    void release_normal_buffers();

    CudaMeshResources shared_buffers;

private:
    float4* previous_position_buffer_ = nullptr;
    float4* velocity_buffer_ = nullptr;
    float* mass_buffer_ = nullptr;
    std::vector<std::uint32_t> triangle_color_counts_;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t triangle_count_ = 0;
};
