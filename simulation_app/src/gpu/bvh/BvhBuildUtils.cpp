#include "gpu/bvh/BvhBuildUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace bvh_build {
namespace {
constexpr std::size_t leaf_size = 3u;

struct NodeContents final
{
    std::size_t primitive_begin;
    std::size_t primitive_end;
    std::uint32_t part_label_mask;
};

struct PartLabelStats final
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t component_count = 0;
};

std::uint32_t find_longest_axis(const glm::vec3& extent)
{
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

bool has_multiple_bits(std::uint32_t mask)
{
    return (mask & (mask - 1u)) != 0u;
}

double surface_area(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    const glm::vec3 extent = glm::max(max_bounds - min_bounds, glm::vec3{0.0f});
    return 2.0 * (static_cast<double>(extent.x) * extent.y +
                  static_cast<double>(extent.y) * extent.z +
                  static_cast<double>(extent.z) * extent.x);
}

bool is_valid_leaf_node(const BvhNode& node, std::uint32_t source_element_count)
{
    return node.element_count > 0u &&
           node.left_child_index == invalid_bvh_node &&
           node.right_child_index == invalid_bvh_node &&
           node.first_element_index <= source_element_count &&
           node.element_count <= source_element_count - node.first_element_index;
}

bool is_valid_internal_node(const BvhNode& node, std::size_t node_index, std::size_t node_count)
{
    return node.element_count == 0u &&
           node.left_child_index > node_index &&
           node.right_child_index > node_index &&
           node.left_child_index != node.right_child_index &&
           node.left_child_index < node_count &&
           node.right_child_index < node_count;
}

bool is_valid_bvh_node_topology(const BvhNode& node,
                                std::size_t node_index,
                                std::size_t node_count,
                                std::uint32_t source_element_count)
{
    if (is_leaf_node(node.element_count)) {
        return is_valid_leaf_node(node, source_element_count);
    }

    return is_valid_internal_node(node, node_index, node_count);
}

bool has_valid_shader_stack_depth(const std::vector<BvhNode>& nodes)
{
    std::vector<std::uint32_t> node_stack;
    node_stack.reserve(shader_max_bvh_stack_depth);
    node_stack.push_back(uploaded_bvh_root_node);

    while (!node_stack.empty()) {
        if (node_stack.size() > shader_max_bvh_stack_depth) {
            return false;
        }

        const std::uint32_t node_index = node_stack.back();
        node_stack.pop_back();

        const BvhNode& node = nodes[node_index];
        if (is_leaf_node(node.element_count)) {
            continue;
        }

        node_stack.push_back(node.right_child_index);
        node_stack.push_back(node.left_child_index);
    }

    return true;
}

std::uint32_t compute_part_label_mask(const std::vector<BvhPrimitive>& primitives,
                                      std::size_t begin,
                                      std::size_t end)
{
    std::uint32_t part_label_mask = 0u;
    for (std::size_t primitive_index = begin; primitive_index < end; ++primitive_index) {
        part_label_mask |= 1u << primitives[primitive_index].part_label;
    }
    return part_label_mask;
}

std::uint32_t find_best_part_label_split_mask(const std::vector<BvhPrimitive>& primitives,
                                              std::size_t begin,
                                              std::size_t end,
                                              std::uint32_t part_label_mask)
{
    std::array<PartLabelStats, body_part_label_count> stats_by_label;
    for (std::size_t primitive_index = begin; primitive_index < end; ++primitive_index) {
        const BvhPrimitive& primitive = primitives[primitive_index];
        PartLabelStats& stats = stats_by_label[primitive.part_label];
        stats.min_bounds = glm::min(stats.min_bounds, primitive.min_bounds);
        stats.max_bounds = glm::max(stats.max_bounds, primitive.max_bounds);
        ++stats.component_count;
    }

    double best_cost = std::numeric_limits<double>::max();
    std::uint32_t best_split_mask = 0u;
    const std::uint32_t anchor_label_mask = part_label_mask & (~part_label_mask + 1u);

    for (std::uint32_t split_mask = (part_label_mask - 1u) & part_label_mask; split_mask != 0u;
         split_mask = (split_mask - 1u) & part_label_mask) {
        if ((split_mask & anchor_label_mask) == 0u) {
            continue;
        }

        PartLabelStats left_stats;
        PartLabelStats right_stats;
        for (std::size_t label = 0; label < stats_by_label.size(); ++label) {
            const std::uint32_t label_mask = 1u << label;
            const PartLabelStats& source_stats = stats_by_label[label];
            if (source_stats.component_count == 0u) {
                continue;
            }

            PartLabelStats& target_stats = (split_mask & label_mask) != 0u ? left_stats : right_stats;
            target_stats.min_bounds = glm::min(target_stats.min_bounds, source_stats.min_bounds);
            target_stats.max_bounds = glm::max(target_stats.max_bounds, source_stats.max_bounds);
            target_stats.component_count += source_stats.component_count;
        }

        if (left_stats.component_count == 0u || right_stats.component_count == 0u) {
            continue;
        }

        const double cost =
            surface_area(left_stats.min_bounds, left_stats.max_bounds) * left_stats.component_count +
            surface_area(right_stats.min_bounds, right_stats.max_bounds) * right_stats.component_count;
        if (best_split_mask == 0u || cost < best_cost) {
            best_cost = cost;
            best_split_mask = split_mask;
        }
    }

    return best_split_mask;
}

std::size_t partition_primitives_by_part_labels(std::vector<BvhPrimitive>& primitives,
                                                std::size_t begin,
                                                std::size_t end,
                                                std::uint32_t left_part_label_mask)
{
    const auto middle = std::partition(primitives.begin() + static_cast<std::ptrdiff_t>(begin),
                                       primitives.begin() + static_cast<std::ptrdiff_t>(end),
                                       [left_part_label_mask](const BvhPrimitive& primitive) {
                                           return (left_part_label_mask & (1u << primitive.part_label)) != 0u;
                                       });
    return static_cast<std::size_t>(middle - primitives.begin());
}

std::size_t partition_primitives(std::vector<BvhPrimitive>& primitives,
                                 std::size_t begin,
                                 std::size_t end,
                                 const glm::vec3& extent)
{
    const std::uint32_t axis = find_longest_axis(extent);
    const std::size_t middle = begin + (end - begin) / 2u;

    std::nth_element(primitives.begin() + static_cast<std::ptrdiff_t>(begin),
                     primitives.begin() + static_cast<std::ptrdiff_t>(middle),
                     primitives.begin() + static_cast<std::ptrdiff_t>(end),
                     [axis](const BvhPrimitive& lhs, const BvhPrimitive& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });

    return middle;
}

std::array<NodeContents, 2> split_node_contents(std::vector<BvhPrimitive>& primitives,
                                                const NodeContents& node,
                                                const glm::vec3& extent)
{
    std::size_t middle;
    std::uint32_t left_part_label_mask = node.part_label_mask;
    std::uint32_t right_part_label_mask = node.part_label_mask;

    if (has_multiple_bits(node.part_label_mask)) {
        left_part_label_mask = find_best_part_label_split_mask(primitives,
                                                               node.primitive_begin,
                                                               node.primitive_end,
                                                               node.part_label_mask);
        right_part_label_mask = node.part_label_mask ^ left_part_label_mask;
        middle = partition_primitives_by_part_labels(primitives,
                                                     node.primitive_begin,
                                                     node.primitive_end,
                                                     left_part_label_mask);
    } else {
        middle = partition_primitives(primitives, node.primitive_begin, node.primitive_end, extent);
    }

    return {NodeContents{node.primitive_begin, middle, left_part_label_mask},
            NodeContents{middle, node.primitive_end, right_part_label_mask}};
}

void compute_node_bounds(const std::vector<BvhPrimitive>& primitives,
                         std::size_t begin,
                         std::size_t end,
                         glm::vec3& min_bounds,
                         glm::vec3& max_bounds)
{
    min_bounds = glm::vec3(std::numeric_limits<float>::max());
    max_bounds = glm::vec3(std::numeric_limits<float>::lowest());

    for (std::size_t primitive_index = begin; primitive_index < end; ++primitive_index) {
        min_bounds = glm::min(min_bounds, primitives[primitive_index].min_bounds);
        max_bounds = glm::max(max_bounds, primitives[primitive_index].max_bounds);
    }
}

}

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    return std::isfinite(min_bounds.x) &&
           std::isfinite(min_bounds.y) &&
           std::isfinite(min_bounds.z) &&
           std::isfinite(max_bounds.x) &&
           std::isfinite(max_bounds.y) &&
           std::isfinite(max_bounds.z);
}

bool is_leaf_node(std::uint32_t component_count)
{
    return component_count > 0u;
}

bool has_valid_bvh_node_topology(const std::vector<BvhNode>& nodes, std::uint32_t source_element_count)
{
    const std::size_t node_count = nodes.size();
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        if (!is_valid_bvh_node_topology(nodes[node_index], node_index, node_count, source_element_count)) {
            return false;
        }
    }

    return has_valid_shader_stack_depth(nodes);
}

BvhTree build_bvh(std::vector<BvhPrimitive> primitives)
{
    BvhTree result;
    if (primitives.empty()) {
        return result;
    }

    const std::size_t leaf_count = (primitives.size() + leaf_size - 1u) / leaf_size;
    result.nodes.reserve(leaf_count * 2u - 1u);
    result.ordered_source_indices.reserve(primitives.size());

    const std::uint32_t part_label_mask = compute_part_label_mask(primitives, 0u, primitives.size());
    std::vector<NodeContents> current_nodes{{0u, primitives.size(), part_label_mask}};

    while (!current_nodes.empty()) {
        const BvhLevelState level_state{static_cast<std::uint32_t>(result.nodes.size()),
                                        static_cast<std::uint32_t>(current_nodes.size())};
        result.levels.push_back(level_state);

        std::vector<NodeContents> next_nodes;
        next_nodes.reserve(current_nodes.size() * 2u);
        const std::uint32_t next_node_start_index = level_state.node_start_index + level_state.node_count;

        for (const NodeContents& node : current_nodes) {
            glm::vec3 min_bounds;
            glm::vec3 max_bounds;
            compute_node_bounds(primitives, node.primitive_begin, node.primitive_end, min_bounds, max_bounds);

            BvhNode& output_node = result.nodes.emplace_back();
            output_node.bounds = {glm::vec4(min_bounds, 0.0f), glm::vec4(max_bounds, 0.0f)};

            const std::size_t primitive_count = node.primitive_end - node.primitive_begin;
            const bool has_multiple_part_labels = has_multiple_bits(node.part_label_mask);
            if (!has_multiple_part_labels && primitive_count <= leaf_size) {
                output_node.first_element_index = static_cast<std::uint32_t>(node.primitive_begin);
                output_node.element_count = static_cast<std::uint32_t>(primitive_count);
                continue;
            }

            const auto [left_child, right_child] =
                split_node_contents(primitives, node, max_bounds - min_bounds);
            output_node.left_child_index =
                next_node_start_index + static_cast<std::uint32_t>(next_nodes.size());
            next_nodes.push_back(left_child);
            output_node.right_child_index =
                next_node_start_index + static_cast<std::uint32_t>(next_nodes.size());
            next_nodes.push_back(right_child);
        }

        current_nodes = std::move(next_nodes);
    }

    for (const BvhPrimitive& primitive : primitives) {
        result.ordered_source_indices.push_back(primitive.source_index);
    }
    std::reverse(result.levels.begin(), result.levels.end());
    return result;
}
}
