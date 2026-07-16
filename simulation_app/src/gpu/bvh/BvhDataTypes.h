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

struct BvhBounds final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
};

static_assert(sizeof(BvhBounds) == 32u);

struct BvhBufferRange final {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct BvhNodeRange final {
    std::uint32_t first_node = 0;
    std::uint32_t node_count = 0;
};

struct GarmentBvhRange final {
    std::uint32_t garment_id = 0;
    std::uint32_t layer = 0;
    BvhBufferRange collision_triangles;
    BvhBufferRange bvh_nodes;
};

struct GarmentBvhLayout final {
    GarmentBvhRange range;
    std::vector<BvhNodeRange> node_ranges_by_level;
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

struct EdgeBvhData final {
    std::vector<std::uint32_t> edge_vertex_indices;
    std::vector<BvhNode> nodes;
    std::vector<BvhNodeRange> node_ranges_by_level;

    std::uint32_t edge_count() const;
    bool is_valid() const;
};
