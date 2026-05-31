#pragma once

#include "gpu/cloth/ClothGpuResources.h"

inline bool is_valid_position_view(const ClothPositionBufferView& position_view)
{
    return position_view.current_position_buffer != 0 &&
           position_view.previous_position_buffer != 0 &&
           position_view.vertex_count != 0;
}
