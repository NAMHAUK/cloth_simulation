#pragma once

#include "gpu/pd/CudaMeshResources.h"

#include <cstdint>

#include <vector_types.h>

class CharacterGpuState;
struct CharacterMotion;

class PdCharacterGpuState final
{
public:
    PdCharacterGpuState() = default;
    PdCharacterGpuState(const PdCharacterGpuState&) = delete;
    PdCharacterGpuState& operator=(const PdCharacterGpuState&) = delete;

    void initialize(const CharacterGpuState& character_state);
    void set_motion(const CharacterMotion& motion);
    void update_positions(float4* positions, std::uint32_t frame_index) const;

    std::uint32_t vertex_count() const;
    std::uint32_t frame_count() const;

    void release();

    CudaMeshResources shared_buffers;

private:
    float4* animation_position_buffer_ = nullptr;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t frame_count_ = 0;
};
