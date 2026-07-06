#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

inline constexpr std::uint32_t invalid_bvh_node = 0xFFFFFFFFu;

struct BvhNode final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
    std::uint32_t left_child_index = invalid_bvh_node;
    std::uint32_t right_child_index = invalid_bvh_node;
    std::uint32_t first_element_index = 0u;
    std::uint32_t element_count = 0u;
};

static_assert(sizeof(BvhNode) == 48u);

struct BvhNodeRange final {
    std::uint32_t first_node = 0;
    std::uint32_t node_count = 0;
};

struct TriangleBvhData final {
    std::vector<std::uint32_t> triangle_indices;
    std::vector<BvhNode> nodes;
    std::vector<BvhNodeRange> node_ranges_by_level;

    bool is_valid(std::uint32_t triangle_count) const;
};

struct VertexBvhData final {
    std::vector<std::uint32_t> vertex_ids;
    std::vector<BvhNode> nodes;
    std::vector<BvhNodeRange> node_ranges_by_level;

    bool is_valid(std::uint32_t vertex_count) const;
};
