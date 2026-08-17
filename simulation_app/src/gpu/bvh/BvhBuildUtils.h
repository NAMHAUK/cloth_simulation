#pragma once

#include "gpu/bvh/BvhDataTypes.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace bvh_build {
inline constexpr std::size_t shader_max_bvh_stack_depth = 32u;
inline constexpr std::size_t body_part_label_count = 8u;
inline constexpr std::uint32_t uploaded_bvh_root_node = 0u;

struct BvhPrimitive final
{
    std::uint32_t source_index = 0;
    glm::vec3 center{};
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint8_t part_label = 0;
};

struct BvhTree final
{
    std::vector<std::uint32_t> ordered_source_indices;
    std::vector<BvhNode> nodes;
    std::vector<BvhLevelState> levels;
};

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds);
bool is_leaf_node(std::uint32_t component_count);
bool has_valid_bvh_node_topology(const std::vector<BvhNode>& nodes, std::uint32_t source_element_count);
BvhTree build_bvh(std::vector<BvhPrimitive> primitives, std::uint32_t leaf_size);
}
