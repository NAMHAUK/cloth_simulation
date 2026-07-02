#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

inline constexpr std::uint32_t invalid_mesh_bvh_node = 0xFFFFFFFFu;

struct MeshBvhNode final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
    std::uint32_t left_child_index = invalid_mesh_bvh_node;
    std::uint32_t right_child_index = invalid_mesh_bvh_node;
    std::uint32_t first_triangle_index = 0u;
    std::uint32_t triangle_count = 0u;
};

static_assert(sizeof(MeshBvhNode) == 48u);

struct BodyVertexBvhNode final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
    std::uint32_t left_child_index = invalid_mesh_bvh_node;
    std::uint32_t right_child_index = invalid_mesh_bvh_node;
    std::uint32_t first_vertex_index = 0u;
    std::uint32_t vertex_count = 0u;
};

static_assert(sizeof(BodyVertexBvhNode) == 48u);

struct BvhNodeRange final {
    std::uint32_t first_node = 0;
    std::uint32_t node_count = 0;
};

struct MeshBvhData final {
    std::vector<std::uint32_t> triangle_indices;
    std::vector<MeshBvhNode> nodes;
    std::vector<BvhNodeRange> node_ranges_by_level;
    std::uint32_t root_node_index = 0;

    bool is_valid(std::uint32_t triangle_count) const;
};

struct BodyVertexBvhData final {
    std::vector<std::uint32_t> vertex_ids;
    std::vector<BodyVertexBvhNode> nodes;
    std::vector<BvhNodeRange> node_ranges_by_level;
    std::uint32_t root_node_index = 0;

    bool is_valid(std::uint32_t vertex_count) const;
};
