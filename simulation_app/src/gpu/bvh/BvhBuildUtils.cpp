#include "gpu/bvh/BvhBuildUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace bvh_build {
namespace {
constexpr std::size_t leaf_size = 3u;
constexpr std::size_t edge_vertex_count = 2u;
constexpr std::size_t triangle_vertex_count = 3u;

struct PrimitiveRange final
{
    std::size_t primitive_begin;
    std::size_t primitive_end;
    std::uint32_t part_label_mask;
};

struct PartLabelStats final
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t primitive_count = 0;
};

// Part label splitting
std::uint32_t compute_part_label_mask(const std::vector<BvhPrimitive>& primitives)
{
    std::uint32_t part_label_mask = 0u;
    for (const BvhPrimitive& primitive : primitives) {
        part_label_mask |= 1u << primitive.part_label;
    }
    return part_label_mask;
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

double part_split_cost(const std::array<PartLabelStats, body_part_label_count>& stats_by_label,
                       std::uint32_t left_part_labels)
{
    PartLabelStats left_stats;
    PartLabelStats right_stats;
    for (std::size_t label = 0; label < stats_by_label.size(); ++label) {
        const PartLabelStats& source_stats = stats_by_label[label];
        if (source_stats.primitive_count == 0u) {
            continue;
        }

        const std::uint32_t label_mask = 1u << label;
        PartLabelStats& target_stats = (left_part_labels & label_mask) != 0u ? left_stats : right_stats;
        target_stats.min_bounds = glm::min(target_stats.min_bounds, source_stats.min_bounds);
        target_stats.max_bounds = glm::max(target_stats.max_bounds, source_stats.max_bounds);
        target_stats.primitive_count += source_stats.primitive_count;
    }

    return surface_area(left_stats.min_bounds, left_stats.max_bounds) * left_stats.primitive_count +
           surface_area(right_stats.min_bounds, right_stats.max_bounds) * right_stats.primitive_count;
}

std::uint32_t choose_part_split(const std::vector<BvhPrimitive>& primitives, const PrimitiveRange& node)
{
    std::array<PartLabelStats, body_part_label_count> stats_by_label;
    for (std::size_t primitive_index = node.primitive_begin; primitive_index < node.primitive_end;
         ++primitive_index) {
        const BvhPrimitive& primitive = primitives[primitive_index];
        PartLabelStats& stats = stats_by_label[primitive.part_label];
        stats.min_bounds = glm::min(stats.min_bounds, primitive.min_bounds);
        stats.max_bounds = glm::max(stats.max_bounds, primitive.max_bounds);
        ++stats.primitive_count;
    }

    double best_cost = std::numeric_limits<double>::max();
    std::uint32_t best_left_labels = 0u;

    for (std::uint32_t left_labels = (node.part_label_mask - 1u) & node.part_label_mask; left_labels != 0u;
         left_labels = (left_labels - 1u) & node.part_label_mask) {
        // Evaluate left/right mirrored splits once.
        const std::uint32_t right_labels = node.part_label_mask ^ left_labels;
        if (left_labels > right_labels) {
            continue;
        }

        const double cost = part_split_cost(stats_by_label, left_labels);
        if (best_left_labels == 0u || cost < best_cost) {
            best_cost = cost;
            best_left_labels = left_labels;
        }
    }

    return best_left_labels;
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

// Spatial splitting
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

// Node construction
std::array<PrimitiveRange, 2> split_primitive_range(std::vector<BvhPrimitive>& primitives,
                                                    const PrimitiveRange& range,
                                                    const glm::vec3& extent)
{
    std::size_t middle;
    std::uint32_t left_part_label_mask = range.part_label_mask;
    std::uint32_t right_part_label_mask = range.part_label_mask;

    if (has_multiple_bits(range.part_label_mask)) {
        left_part_label_mask = choose_part_split(primitives, range);
        right_part_label_mask = range.part_label_mask ^ left_part_label_mask;
        middle = partition_primitives_by_part_labels(primitives,
                                                     range.primitive_begin,
                                                     range.primitive_end,
                                                     left_part_label_mask);
    } else {
        middle = partition_primitives(primitives, range.primitive_begin, range.primitive_end, extent);
    }

    return {PrimitiveRange{range.primitive_begin, middle, left_part_label_mask},
            PrimitiveRange{middle, range.primitive_end, right_part_label_mask}};
}

Aabb compute_node_bounds(const std::vector<BvhPrimitive>& primitives, std::size_t begin, std::size_t end)
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};

    for (std::size_t primitive_index = begin; primitive_index < end; ++primitive_index) {
        min_bounds = glm::min(min_bounds, primitives[primitive_index].min_bounds);
        max_bounds = glm::max(max_bounds, primitives[primitive_index].max_bounds);
    }

    return {glm::vec4(min_bounds, 0.0f), glm::vec4(max_bounds, 0.0f)};
}

BvhNode build_node(std::vector<BvhPrimitive>& primitives,
                   const PrimitiveRange& range,
                   std::uint32_t child_first_node_index,
                   std::vector<PrimitiveRange>& child_level_ranges)
{
    BvhNode node;
    node.bounds = compute_node_bounds(primitives, range.primitive_begin, range.primitive_end);

    const std::size_t primitive_count = range.primitive_end - range.primitive_begin;
    if (!has_multiple_bits(range.part_label_mask) && primitive_count <= leaf_size) {
        node.first_element_index = static_cast<std::uint32_t>(range.primitive_begin);
        node.element_count = static_cast<std::uint32_t>(primitive_count);
        return node;
    }

    const glm::vec3 extent = glm::vec3(node.bounds.max_bounds - node.bounds.min_bounds);
    const auto [left_child, right_child] = split_primitive_range(primitives, range, extent);
    node.left_child_index = child_first_node_index + static_cast<std::uint32_t>(child_level_ranges.size());
    child_level_ranges.push_back(left_child);
    node.right_child_index = child_first_node_index + static_cast<std::uint32_t>(child_level_ranges.size());
    child_level_ranges.push_back(right_child);
    return node;
}

std::vector<PrimitiveRange> build_level(std::vector<BvhPrimitive>& primitives,
                                        const std::vector<PrimitiveRange>& current_level_ranges,
                                        Bvh& bvh)
{
    const std::uint32_t first_node_index = static_cast<std::uint32_t>(bvh.nodes.size());
    const std::uint32_t current_level_node_count = static_cast<std::uint32_t>(current_level_ranges.size());
    const std::uint32_t child_first_node_index = first_node_index + current_level_node_count;
    bvh.level_offsets.push_back(first_node_index);

    std::vector<PrimitiveRange> child_level_ranges;
    child_level_ranges.reserve(current_level_ranges.size() * 2u);

    for (const PrimitiveRange& range : current_level_ranges) {
        bvh.nodes.push_back(build_node(primitives, range, child_first_node_index, child_level_ranges));
    }

    return child_level_ranges;
}

// state
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

bool has_valid_bvh_level_offsets(const std::vector<std::uint32_t>& level_offsets, std::size_t node_count)
{
    if (level_offsets.size() < 2u || level_offsets.front() != 0u || level_offsets.back() != node_count) {
        return false;
    }

    return std::adjacent_find(level_offsets.begin(),
                              level_offsets.end(),
                              [](std::uint32_t first, std::uint32_t second) { return first >= second; }) ==
           level_offsets.end();
}

std::uint32_t leaf_element_count(const std::vector<BvhNode>& nodes)
{
    std::uint32_t element_count = 0u;
    for (const BvhNode& node : nodes) {
        if (is_leaf_node(node.element_count)) {
            element_count += node.element_count;
        }
    }
    return element_count;
}

bool is_valid_triangle_bvh(const Bvh& bvh, std::uint32_t triangle_count)
{
    const std::uint32_t collision_triangle_count = leaf_element_count(bvh.nodes);
    return triangle_count != 0u &&
           collision_triangle_count != 0u &&
           collision_triangle_count <= triangle_count &&
           bvh.indices.size() == triangle_count * triangle_vertex_count &&
           has_valid_bvh_level_offsets(bvh.level_offsets, bvh.nodes.size()) &&
           has_valid_bvh_node_topology(bvh.nodes, collision_triangle_count);
}

bool is_valid_vertex_bvh(const Bvh& bvh, std::uint32_t vertex_count)
{
    if (vertex_count == 0u ||
        bvh.indices.empty() ||
        bvh.indices.size() > vertex_count ||
        !has_valid_bvh_level_offsets(bvh.level_offsets, bvh.nodes.size()) ||
        !has_valid_bvh_node_topology(bvh.nodes, static_cast<std::uint32_t>(bvh.indices.size()))) {
        return false;
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    std::size_t used_vertex_count = 0u;
    for (const BvhNode& node : bvh.nodes) {
        if (is_leaf_node(node.element_count)) {
            for (std::uint32_t offset = 0; offset < node.element_count; ++offset) {
                const std::uint32_t vertex_index = bvh.indices[node.first_element_index + offset];
                if (vertex_index >= vertex_count || used_vertices[vertex_index] != 0u) {
                    return false;
                }
                used_vertices[vertex_index] = 1u;
                ++used_vertex_count;
            }
        }
    }

    return used_vertex_count == bvh.indices.size();
}

bool is_valid_edge_bvh(const Bvh& bvh)
{
    if (bvh.indices.empty() ||
        bvh.indices.size() % edge_vertex_count != 0u ||
        !has_valid_bvh_level_offsets(bvh.level_offsets, bvh.nodes.size())) {
        return false;
    }

    const auto edge_count = static_cast<std::uint32_t>(bvh.indices.size() / edge_vertex_count);
    if (!has_valid_bvh_node_topology(bvh.nodes, edge_count)) {
        return false;
    }

    std::vector<std::uint8_t> used_edges(edge_count, 0u);
    for (const BvhNode& node : bvh.nodes) {
        if (!is_leaf_node(node.element_count)) {
            continue;
        }

        for (std::uint32_t offset = 0; offset < node.element_count; ++offset) {
            const std::uint32_t edge_index = node.first_element_index + offset;
            if (edge_index >= edge_count || used_edges[edge_index] != 0u) {
                return false;
            }
            used_edges[edge_index] = 1u;
            if (bvh.indices[edge_index * edge_vertex_count] ==
                bvh.indices[edge_index * edge_vertex_count + 1u]) {
                return false;
            }
        }
    }

    return std::all_of(used_edges.begin(), used_edges.end(), [](std::uint8_t used) { return used != 0u; });
}

Bvh build_bvh(std::vector<BvhPrimitive>& primitives)
{
    assert(!primitives.empty());

    Bvh bvh;
    bvh.nodes.reserve(primitives.size() * 2u - 1u);

    const PrimitiveRange root_range{0u, primitives.size(), compute_part_label_mask(primitives)};
    std::vector<PrimitiveRange> current_level_ranges{root_range};

    while (!current_level_ranges.empty()) {
        current_level_ranges = build_level(primitives, current_level_ranges, bvh);
    }

    bvh.level_offsets.push_back(static_cast<std::uint32_t>(bvh.nodes.size()));
    return bvh;
}
}
