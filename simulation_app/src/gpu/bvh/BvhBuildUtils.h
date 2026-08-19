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
    std::uint32_t element_index = 0;
    glm::vec3 center{};
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint8_t part_label = 0;
};

bool is_leaf_node(std::uint32_t component_count);
std::uint32_t leaf_element_count(const std::vector<BvhNode>& nodes);
bool is_valid_triangle_bvh(const Bvh& bvh, std::uint32_t triangle_count);
bool is_valid_vertex_bvh(const Bvh& bvh, std::uint32_t vertex_count);
bool is_valid_edge_bvh(const Bvh& bvh);
Bvh build_bvh(std::vector<BvhPrimitive>& primitives);
}
