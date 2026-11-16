#pragma once

#include <cstdint>

#include <vector_types.h>

class CharacterGpuState;
struct CharacterMotion;
struct TriangleVertexIndices;
struct cudaGraphicsResource;

class PdCharacterGpuState final
{
public:
    PdCharacterGpuState() = default;
    PdCharacterGpuState(const PdCharacterGpuState&) = delete;
    PdCharacterGpuState& operator=(const PdCharacterGpuState&) = delete;

    void initialize(const CharacterGpuState& character_state);
    void set_motion(const CharacterMotion& motion);
    void update_positions(float4* positions, std::uint32_t frame_index) const;

    void map_shared_buffers(float4*& positions, float4*& normals);
    void unmap_shared_buffers();

    const TriangleVertexIndices* triangle_vertex_indices_buffer() const;
    std::uint32_t vertex_count() const;
    std::uint32_t frame_count() const;

    void release();

private:
    cudaGraphicsResource* position_resource_ = nullptr;
    cudaGraphicsResource* normal_resource_ = nullptr;
    cudaGraphicsResource* triangle_resource_ = nullptr;
    float4* animation_position_buffer_ = nullptr;
    const TriangleVertexIndices* triangle_vertex_indices_buffer_ = nullptr;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t frame_count_ = 0;
};
