#pragma once

#include "gpu/cloth/ClothGpuResources.h"

inline bool is_valid_position_view(const ClothPositionBufferView& position_view)
{
    return position_view.current_position_buffer != 0 &&
           position_view.previous_position_buffer != 0 &&
           position_view.vertex_count != 0;
}

inline bool is_valid_distance_constraint_view(const DistanceConstraintBufferView& constraint_view)
{
    return constraint_view.edge_index_buffer != 0 &&
           constraint_view.rest_length_buffer != 0 &&
           constraint_view.constraint_count != 0 &&
           constraint_view.color_ranges != nullptr &&
           !constraint_view.color_ranges->empty();
}
