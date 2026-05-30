#pragma once

#include <cstdint>

constexpr std::uint32_t compute_group_count(std::uint32_t item_count, std::uint32_t local_size)
{
    return (item_count + local_size - 1u) / local_size;
}
