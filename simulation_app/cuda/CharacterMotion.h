#pragma once

#include <cstdint>

#include <vector_types.h>

class PdCharacterGpuState;

void interpolate_character_positions(float4* positions,
                                     const PdCharacterGpuState& character_state,
                                     std::uint32_t frame_index,
                                     std::uint32_t next_frame_index,
                                     float frame_alpha);
