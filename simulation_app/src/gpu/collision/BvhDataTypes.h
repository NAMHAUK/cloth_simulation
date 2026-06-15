#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

inline constexpr std::uint32_t invalid_mesh_bvh_node = 0xFFFFFFFFu;

struct MeshBvhNode final {
    glm::vec4 min_bounds{};
    glm::vec4 max_bounds{};
    glm::uvec4 metadata{invalid_mesh_bvh_node, invalid_mesh_bvh_node, invalid_mesh_bvh_node, 0u};
};

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
