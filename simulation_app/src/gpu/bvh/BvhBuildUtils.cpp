#include "gpu/bvh/BvhBuildUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace bvh_build {
namespace {
struct BvhBuildNode final {
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint32_t left_child_index = invalid_bvh_node;
    std::uint32_t right_child_index = invalid_bvh_node;
    std::uint32_t first_element_index = 0;
    std::uint32_t element_count = 0;
};

struct BvhBuildContext final {
    std::vector<BvhPrimitive>& primitives;
    std::vector<BvhBuildNode>& nodes;
    std::vector<std::uint32_t>& ordered_primitive_indices;
    std::uint32_t leaf_size = 8u;
    bool split_by_part_labels = false;
};

struct PartLabelStats final {
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t component_count = 0;
};

struct NextBvhLevel final {
    explicit NextBvhLevel(const BvhNodeRange& current_level): first_node(current_level.first_node + current_level.node_count)
    {
        node_indices.reserve(static_cast<std::size_t>(current_level.node_count) * 2u);
    }

    std::vector<std::uint32_t> node_indices;
    std::uint32_t first_node = 0;
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

    for (std::uint32_t split_mask = (part_label_mask - 1u) & part_label_mask;
         split_mask != 0u;
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
        if (cost < best_cost) {
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

std::optional<std::size_t> split_mixed_part_labels(std::vector<BvhPrimitive>& primitives,
                                                   std::size_t begin,
                                                   std::size_t end,
                                                   bool enabled)
{
    if (!enabled) {
        return std::nullopt;
    }

    const std::uint32_t part_label_mask = compute_part_label_mask(primitives, begin, end);
    if (!has_multiple_bits(part_label_mask)) {
        return std::nullopt;
    }

    const std::uint32_t left_part_label_mask = find_best_part_label_split_mask(primitives, begin, end, part_label_mask);
    if (left_part_label_mask == 0u) {
        return std::nullopt;
    }

    return partition_primitives_by_part_labels(primitives, begin, end, left_part_label_mask);
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

void write_leaf_node_data(BvhBuildContext& context, BvhBuildNode& node, std::size_t begin, std::size_t end)
{
    node.first_element_index = static_cast<std::uint32_t>(context.ordered_primitive_indices.size());
    node.element_count = static_cast<std::uint32_t>(end - begin);

    for (std::size_t primitive_index = begin; primitive_index < end; ++primitive_index) {
        context.ordered_primitive_indices.push_back(context.primitives[primitive_index].primitive_index);
    }
}

std::uint32_t build_bvh_tree(BvhBuildContext& context, std::size_t begin, std::size_t end)
{
    const std::uint32_t node_index = static_cast<std::uint32_t>(context.nodes.size());
    context.nodes.emplace_back();

    BvhBuildNode& node = context.nodes.back();
    compute_node_bounds(context.primitives, begin, end, node.min_bounds, node.max_bounds);

    const std::size_t primitive_count = end - begin;
    if (const auto middle = split_mixed_part_labels(context.primitives, begin, end, context.split_by_part_labels)) {
        const std::uint32_t left_child_index = build_bvh_tree(context, begin, *middle);
        const std::uint32_t right_child_index = build_bvh_tree(context, *middle, end);
        context.nodes[node_index].left_child_index = left_child_index;
        context.nodes[node_index].right_child_index = right_child_index;

        return node_index;
    }
    if (primitive_count <= context.leaf_size) {
        write_leaf_node_data(context, node, begin, end);
        return node_index;
    }

    const std::size_t middle =
        partition_primitives(context.primitives, begin, end, node.max_bounds - node.min_bounds);

    const std::uint32_t left_child_index = build_bvh_tree(context, begin, middle);
    const std::uint32_t right_child_index = build_bvh_tree(context, middle, end);
    context.nodes[node_index].left_child_index = left_child_index;
    context.nodes[node_index].right_child_index = right_child_index;

    return node_index;
}

void write_level_ordered_bvh_data(std::uint32_t source_root_node,
                                  const std::vector<BvhBuildNode>& build_nodes,
                                  std::vector<BvhNode>& result_nodes,
                                  std::vector<BvhNodeRange>& result_node_ranges_by_level)
{
    std::vector<BvhNodeRange> level_order_node_ranges;
    level_order_node_ranges.reserve(build_nodes.size());
    result_nodes.clear();
    result_nodes.reserve(build_nodes.size());

    std::vector<std::uint32_t> current_level_node_indices{source_root_node};

    while (!current_level_node_indices.empty()) {
        const BvhNodeRange level_range{
            static_cast<std::uint32_t>(result_nodes.size()),
            static_cast<std::uint32_t>(current_level_node_indices.size())
        };
        level_order_node_ranges.push_back(level_range);
        NextBvhLevel next_level(level_range);

        for (const std::uint32_t build_node_index : current_level_node_indices) {
            const auto& build_node = build_nodes[build_node_index];
            BvhNode& node = result_nodes.emplace_back();
            node.bounds = {glm::vec4(build_node.min_bounds, 0.0f), glm::vec4(build_node.max_bounds, 0.0f)};

            if (build_node.element_count > 0u) {
                node.first_element_index = build_node.first_element_index;
                node.element_count = build_node.element_count;
            } else {
                node.left_child_index =
                    next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
                next_level.node_indices.push_back(build_node.left_child_index);

                node.right_child_index =
                    next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
                next_level.node_indices.push_back(build_node.right_child_index);
            }
        }

        current_level_node_indices = std::move(next_level.node_indices);
    }

    result_node_ranges_by_level.assign(level_order_node_ranges.rbegin(), level_order_node_ranges.rend());
}
}

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    return std::isfinite(min_bounds.x) && std::isfinite(min_bounds.y) && std::isfinite(min_bounds.z) &&
           std::isfinite(max_bounds.x) && std::isfinite(max_bounds.y) && std::isfinite(max_bounds.z);
}

bool is_leaf_node(std::uint32_t component_count)
{
    return component_count > 0u;
}

bool has_valid_bvh_node_topology(const std::vector<BvhNode>& nodes,
                                 std::uint32_t source_element_count)
{
    const std::size_t node_count = nodes.size();
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        if (!is_valid_bvh_node_topology(nodes[node_index], node_index, node_count, source_element_count)) {
            return false;
        }
    }

    return has_valid_shader_stack_depth(nodes);
}

BvhTree build_bvh(std::vector<BvhPrimitive> primitives,
                  std::uint32_t leaf_size,
                  bool split_by_part_labels)
{
    BvhTree result;
    if (primitives.empty() || leaf_size == 0u) {
        return result;
    }

    const std::size_t leaf_count = (primitives.size() + leaf_size - 1u) / leaf_size;
    std::vector<BvhBuildNode> build_nodes;
    build_nodes.reserve(leaf_count * 2u - 1u);
    result.ordered_primitive_indices.reserve(primitives.size());

    BvhBuildContext context{
        primitives,
        build_nodes,
        result.ordered_primitive_indices,
        leaf_size,
        split_by_part_labels
    };
    const std::uint32_t source_root_node = build_bvh_tree(context, 0u, primitives.size());
    write_level_ordered_bvh_data(
        source_root_node,
        build_nodes,
        result.nodes,
        result.node_ranges_by_level
    );
    return result;
}
}
