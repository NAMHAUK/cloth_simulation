#pragma once

#include "assets/MotionAsset.h"
#include "character/CharacterGpuState.h"

#include <cstdint>
#include <limits>

struct CharacterInstance {
    CharacterMesh mesh;
    CharacterGpuState gpu_state;
    bool loaded = false;
    std::uint32_t current_frame = 0;
    std::uint32_t uploaded_frame = std::numeric_limits<std::uint32_t>::max();
};
