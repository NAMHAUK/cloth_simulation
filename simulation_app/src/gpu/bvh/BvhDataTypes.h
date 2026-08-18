#pragma once

#include "asset/AssetDataTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <QOpenGLFunctions_4_5_Core>

inline constexpr std::uint32_t invalid_bvh_node = 0xFFFFFFFFu;

struct Aabb final
{
    alignas(16) glm::vec4 min_bounds{};
    alignas(16) glm::vec4 max_bounds{};
};

static_assert(offsetof(Aabb, max_bounds) == 16u);
static_assert(sizeof(Aabb) == 32u);

struct BvhNode final
{
    Aabb bounds{};
    std::uint32_t left_child_index = invalid_bvh_node;
    std::uint32_t right_child_index = invalid_bvh_node;
    std::uint32_t first_element_index = 0u;
    std::uint32_t element_count = 0u;
};

static_assert(sizeof(BvhNode) == 48u);

struct Bvh final
{
    std::vector<BvhNode> nodes;
    std::vector<std::uint32_t> level_offsets;
    std::vector<std::uint32_t> indices;
};

struct BvhBufferView final
{
    GLuint node_buffer = 0;
    GLuint bounds_buffer = 0;
    std::uint32_t node_count = 0;
};
