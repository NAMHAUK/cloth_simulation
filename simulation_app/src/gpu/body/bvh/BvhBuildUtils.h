#pragma once

#include "gpu/body/bvh/BvhDataTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace bvh_build {
inline constexpr std::size_t shader_max_bvh_stack_depth = 32u;
inline constexpr std::size_t character_part_label_count = 6u;
inline constexpr std::uint32_t uploaded_bvh_root_node = 0u;

struct DefaultBodyElement final {
    std::uint32_t element_index = 0;
    glm::vec3 center{};
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint8_t part_label = 0;
};

struct BvhBuildNode final {
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint32_t left_child_index = invalid_bvh_node;
    std::uint32_t right_child_index = invalid_bvh_node;
    std::uint32_t first_element_index = 0;
    std::uint32_t element_count = 0;
};

struct BvhBuildContext final {
    std::vector<DefaultBodyElement>& elements;
    std::vector<BvhBuildNode>& nodes;
    std::vector<std::uint32_t>& element_indices;
    std::uint32_t leaf_size = 8u;
    bool split_by_part_labels = false;
};

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds);
bool is_leaf_node(std::uint32_t component_count);
bool has_valid_bvh_node_topology(const std::vector<BvhNode>& nodes,
                                 std::uint32_t source_element_count);
std::uint32_t build_bvh_tree(BvhBuildContext& context, std::size_t begin, std::size_t end);
void write_level_ordered_bvh_data(std::uint32_t source_root_node,
                                  const std::vector<BvhBuildNode>& build_nodes,
                                  std::vector<BvhNode>& result_nodes,
                                  std::vector<BvhNodeRange>& result_node_ranges_by_level);
}
