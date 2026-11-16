#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;
struct TriangleVertexIndices;
struct cudaGraphicsResource;

class PdClothGpuState final
{
public:
    PdClothGpuState() = default;
    PdClothGpuState(const PdClothGpuState&) = delete;
    PdClothGpuState& operator=(const PdClothGpuState&) = delete;

    void rebuild_buffers(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void prepare_normal_buffers(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void initialize_masses(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    void reset_velocities() const;

    void map_shared_buffers(float4*& positions, float4*& normals);
    void unmap_shared_buffers();
    float4* previous_position_buffer() const;
    float4* predicted_position_buffer() const;
    float4* velocity_buffer() const;
    const float* mass_buffer() const;
    const TriangleVertexIndices* triangle_vertex_indices_buffer() const;
    const std::vector<std::uint32_t>& triangle_color_counts() const;
    std::uint32_t vertex_count() const;
    std::uint32_t triangle_count() const;

    void release();

private:
    cudaGraphicsResource* position_resource_ = nullptr;
    cudaGraphicsResource* normal_resource_ = nullptr;
    cudaGraphicsResource* triangle_resource_ = nullptr;
    float4* previous_position_buffer_ = nullptr;
    float4* predicted_position_buffer_ = nullptr;
    float4* velocity_buffer_ = nullptr;
    float* mass_buffer_ = nullptr;
    const TriangleVertexIndices* triangle_vertex_indices_buffer_ = nullptr;
    std::vector<std::uint32_t> triangle_color_counts_;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t triangle_count_ = 0;
};
