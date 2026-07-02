#include "gpu/body/bvh/VertexBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t vertex_position_component_count = 3;
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t vertex_bvh_leaf_size = 8;
constexpr std::size_t shader_max_bvh_stack_depth = 32u;
constexpr std::size_t character_part_label_count = 6u;

struct PartLabelStats final {
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t vertex_count = 0;
};

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    return std::isfinite(min_bounds.x) && std::isfinite(min_bounds.y) && std::isfinite(min_bounds.z) &&
           std::isfinite(max_bounds.x) && std::isfinite(max_bounds.y) && std::isfinite(max_bounds.z);
}

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

bool is_leaf_node(const BodyVertexBvhNode& node)
{
    return node.vertex_count > 0u;
}

bool is_valid_leaf_node(const BodyVertexBvhNode& node, std::uint32_t source_vertex_count)
{
    return node.vertex_count > 0u &&
           node.left_child_index == invalid_mesh_bvh_node &&
           node.right_child_index == invalid_mesh_bvh_node &&
           node.first_vertex_index <= source_vertex_count &&
           node.vertex_count <= source_vertex_count - node.first_vertex_index;
}

bool is_valid_internal_node(const BodyVertexBvhNode& node, std::size_t node_index, std::size_t node_count)
{
    return node.vertex_count == 0u &&
           node.left_child_index > node_index &&
           node.right_child_index > node_index &&
           node.left_child_index != node.right_child_index &&
           node.left_child_index < node_count &&
           node.right_child_index < node_count;
}

bool has_valid_shader_stack_depth(const BodyVertexBvhData& bvh)
{
    std::vector<std::uint32_t> node_stack;
    node_stack.reserve(shader_max_bvh_stack_depth);
    node_stack.push_back(bvh.root_node_index);

    while (!node_stack.empty()) {
        if (node_stack.size() > shader_max_bvh_stack_depth) {
            return false;
        }

        const std::uint32_t node_index = node_stack.back();
        node_stack.pop_back();

        const BodyVertexBvhNode& node = bvh.nodes[node_index];
        if (is_leaf_node(node)) {
            continue;
        }

        node_stack.push_back(node.right_child_index);
        node_stack.push_back(node.left_child_index);
    }

    return true;
}
}

VertexBvhBuilder::VertexBvhBuilder(std::uint32_t vertex_count, const std::vector<float>& vertices)
    : vertex_count_(vertex_count),
      vertices_(vertices)
{
}

VertexBvhBuilder::VertexBvhBuilder(std::uint32_t vertex_count,
                                   const std::vector<std::uint32_t>& triangle_indices,
                                   const std::vector<float>& vertices,
                                   const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(vertex_count),
      source_triangle_indices_(&triangle_indices),
      vertices_(vertices),
      triangle_part_labels_(&triangle_part_labels)
{
}

BodyVertexBvhData VertexBvhBuilder::build_body_vertex_bvh()
{
    BodyVertexBvhData result;

    if (!build_vertex_items()) {
        return result;
    }

    const std::size_t leaf_count = (vertex_items_.size() + vertex_bvh_leaf_size - 1u) / vertex_bvh_leaf_size;
    build_nodes_.reserve(leaf_count * 2u - 1u);
    result.vertex_ids.reserve(vertex_items_.size());

    const std::uint32_t root_node_index = build_bvh_tree(0u, vertex_items_.size(), result.vertex_ids);
    write_level_ordered_bvh_data(root_node_index, result);

    result.root_node_index = 0;
    return result;
}

bool VertexBvhBuilder::build_vertex_items()
{
    if (vertex_count_ == 0 ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return false;
    }

    std::vector<std::uint8_t> vertex_part_labels;
    if (has_part_labels() && !build_vertex_part_labels(vertex_part_labels)) {
        return false;
    }

    vertex_items_.clear();
    vertex_items_.reserve(vertex_count_);

    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        const glm::vec3 position = get_vertex_position(vertices_, vertex_id);
        if (!is_valid_bounds(position, position)) {
            return false;
        }

        vertex_items_.push_back({
            vertex_id,
            position,
            position,
            position,
            has_part_labels() ? vertex_part_labels[vertex_id] : 0u
        });
    }

    return true;
}

bool VertexBvhBuilder::has_part_labels() const
{
    return source_triangle_indices_ != nullptr &&
           triangle_part_labels_ != nullptr &&
           !triangle_part_labels_->empty();
}

bool VertexBvhBuilder::build_vertex_part_labels(std::vector<std::uint8_t>& vertex_part_labels) const
{
    if (source_triangle_indices_ == nullptr ||
        source_triangle_indices_->empty() ||
        source_triangle_indices_->size() % triangle_vertex_count != 0u) {
        return false;
    }

    const std::uint32_t triangle_count =
        static_cast<std::uint32_t>(source_triangle_indices_->size() / triangle_vertex_count);
    if (triangle_part_labels_ == nullptr || triangle_part_labels_->size() != triangle_count) {
        return false;
    }

    std::vector<std::array<std::uint32_t, character_part_label_count>> label_counts(vertex_count_);
    for (auto& counts : label_counts) {
        counts.fill(0u);
    }

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = (*triangle_part_labels_)[triangle_index];
        if (part_label >= character_part_label_count) {
            return false;
        }

        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_id = (*source_triangle_indices_)[index_base + index_offset];
            if (vertex_id >= vertex_count_) {
                return false;
            }

            ++label_counts[vertex_id][part_label];
        }
    }

    vertex_part_labels.assign(vertex_count_, 0u);
    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        std::uint32_t best_count = 0u;
        std::uint8_t best_label = 0u;
        for (std::uint8_t label = 0u; label < character_part_label_count; ++label) {
            const std::uint32_t current_count = label_counts[vertex_id][label];
            if (current_count > best_count) {
                best_count = current_count;
                best_label = label;
            }
        }
        vertex_part_labels[vertex_id] = best_label;
    }

    return true;
}

std::uint32_t VertexBvhBuilder::build_bvh_tree(std::size_t begin,
                                               std::size_t end,
                                               std::vector<std::uint32_t>& vertex_ids)
{
    const std::uint32_t node_index = static_cast<std::uint32_t>(build_nodes_.size());
    build_nodes_.emplace_back();

    BvhBuildNode& node = build_nodes_.back();
    compute_node_bounds(begin, end, node.min_bounds, node.max_bounds);

    const std::size_t vertex_count = end - begin;
    if (has_part_labels()) {
        const std::uint32_t part_label_mask = compute_part_label_mask(begin, end);
        if (has_multiple_bits(part_label_mask)) {
            const std::uint32_t left_part_label_mask = find_best_part_label_split_mask(begin, end, part_label_mask);
            if (left_part_label_mask != 0u) {
                const std::size_t middle =
                    partition_vertex_items_by_part_labels(begin, end, left_part_label_mask);

                const std::uint32_t left_child_index = build_bvh_tree(begin, middle, vertex_ids);
                const std::uint32_t right_child_index = build_bvh_tree(middle, end, vertex_ids);
                build_nodes_[node_index].left_child_index = left_child_index;
                build_nodes_[node_index].right_child_index = right_child_index;

                return node_index;
            }
        }
    }
    if (vertex_count <= vertex_bvh_leaf_size) {
        write_leaf_node_data(node, begin, end, vertex_ids);
        return node_index;
    }

    const std::size_t middle = partition_vertex_items(begin, end, node.max_bounds - node.min_bounds);

    const std::uint32_t left_child_index = build_bvh_tree(begin, middle, vertex_ids);
    const std::uint32_t right_child_index = build_bvh_tree(middle, end, vertex_ids);
    build_nodes_[node_index].left_child_index = left_child_index;
    build_nodes_[node_index].right_child_index = right_child_index;

    return node_index;
}

std::uint32_t VertexBvhBuilder::compute_part_label_mask(std::size_t begin, std::size_t end) const
{
    std::uint32_t part_label_mask = 0u;
    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        part_label_mask |= 1u << vertex_items_[item_index].part_label;
    }
    return part_label_mask;
}

std::uint32_t VertexBvhBuilder::find_best_part_label_split_mask(std::size_t begin,
                                                                std::size_t end,
                                                                std::uint32_t part_label_mask) const
{
    std::array<PartLabelStats, character_part_label_count> stats_by_label;
    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        const VertexBuildItem& item = vertex_items_[item_index];
        PartLabelStats& stats = stats_by_label[item.part_label];
        stats.min_bounds = glm::min(stats.min_bounds, item.min_bounds);
        stats.max_bounds = glm::max(stats.max_bounds, item.max_bounds);
        ++stats.vertex_count;
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
        const std::uint32_t right_split_mask = part_label_mask ^ split_mask;
        for (std::size_t label = 0; label < stats_by_label.size(); ++label) {
            const std::uint32_t label_mask = 1u << label;
            const PartLabelStats& source_stats = stats_by_label[label];
            if (source_stats.vertex_count == 0u) {
                continue;
            }

            PartLabelStats& target_stats = (split_mask & label_mask) != 0u ? left_stats : right_stats;
            target_stats.min_bounds = glm::min(target_stats.min_bounds, source_stats.min_bounds);
            target_stats.max_bounds = glm::max(target_stats.max_bounds, source_stats.max_bounds);
            target_stats.vertex_count += source_stats.vertex_count;
        }

        if (left_stats.vertex_count == 0u || right_stats.vertex_count == 0u || right_split_mask == 0u) {
            continue;
        }

        const double cost =
            surface_area(left_stats.min_bounds, left_stats.max_bounds) * left_stats.vertex_count +
            surface_area(right_stats.min_bounds, right_stats.max_bounds) * right_stats.vertex_count;
        if (cost < best_cost) {
            best_cost = cost;
            best_split_mask = split_mask;
        }
    }

    return best_split_mask;
}

std::size_t VertexBvhBuilder::partition_vertex_items_by_part_labels(std::size_t begin,
                                                                    std::size_t end,
                                                                    std::uint32_t left_part_label_mask)
{
    const auto middle = std::partition(vertex_items_.begin() + static_cast<std::ptrdiff_t>(begin),
                                       vertex_items_.begin() + static_cast<std::ptrdiff_t>(end),
                                       [left_part_label_mask](const VertexBuildItem& item) {
                                           return (left_part_label_mask & (1u << item.part_label)) != 0u;
                                       });
    return static_cast<std::size_t>(middle - vertex_items_.begin());
}

std::size_t VertexBvhBuilder::partition_vertex_items(std::size_t begin,
                                                     std::size_t end,
                                                     const glm::vec3& extent)
{
    const std::uint32_t axis = find_longest_axis(extent);
    const std::size_t middle = begin + (end - begin) / 2u;

    std::nth_element(vertex_items_.begin() + static_cast<std::ptrdiff_t>(begin),
                     vertex_items_.begin() + static_cast<std::ptrdiff_t>(middle),
                     vertex_items_.begin() + static_cast<std::ptrdiff_t>(end),
                     [axis](const VertexBuildItem& lhs, const VertexBuildItem& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });

    return middle;
}

void VertexBvhBuilder::write_leaf_node_data(BvhBuildNode& node,
                                            std::size_t begin,
                                            std::size_t end,
                                            std::vector<std::uint32_t>& vertex_ids) const
{
    node.first_vertex_index = static_cast<std::uint32_t>(vertex_ids.size());
    node.vertex_count = static_cast<std::uint32_t>(end - begin);

    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        vertex_ids.push_back(vertex_items_[item_index].vertex_id);
    }
}

void VertexBvhBuilder::compute_node_bounds(std::size_t begin,
                                           std::size_t end,
                                           glm::vec3& min_bounds,
                                           glm::vec3& max_bounds) const
{
    min_bounds = glm::vec3(std::numeric_limits<float>::max());
    max_bounds = glm::vec3(std::numeric_limits<float>::lowest());

    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        min_bounds = glm::min(min_bounds, vertex_items_[item_index].min_bounds);
        max_bounds = glm::max(max_bounds, vertex_items_[item_index].max_bounds);
    }
}

void VertexBvhBuilder::write_level_ordered_bvh_data(std::uint32_t root_node_index,
                                                    BodyVertexBvhData& result) const
{
    std::vector<BvhNodeRange> level_order_node_ranges;
    level_order_node_ranges.reserve(build_nodes_.size());
    result.nodes.clear();
    result.nodes.reserve(build_nodes_.size());

    std::vector<std::uint32_t> current_level_node_indices{root_node_index};

    while (!current_level_node_indices.empty()) {
        const BvhNodeRange level_range{
            static_cast<std::uint32_t>(result.nodes.size()),
            static_cast<std::uint32_t>(current_level_node_indices.size())
        };
        level_order_node_ranges.push_back(level_range);
        NextBvhLevel next_level(level_range);

        for (const std::uint32_t build_node_index : current_level_node_indices) {
            append_bvh_node(result.nodes, build_node_index, next_level);
        }

        current_level_node_indices = std::move(next_level.node_indices);
    }

    result.node_ranges_by_level.clear();
    result.node_ranges_by_level.reserve(level_order_node_ranges.size());
    for (auto range = level_order_node_ranges.rbegin(); range != level_order_node_ranges.rend(); ++range) {
        result.node_ranges_by_level.push_back(*range);
    }
}

void VertexBvhBuilder::append_bvh_node(std::vector<BodyVertexBvhNode>& result_nodes,
                                       std::uint32_t build_node_index,
                                       NextBvhLevel& next_level) const
{
    const BvhBuildNode& build_node = build_nodes_[build_node_index];
    BodyVertexBvhNode& node = result_nodes.emplace_back();
    node.min_bounds = glm::vec4(build_node.min_bounds, 0.0f);
    node.max_bounds = glm::vec4(build_node.max_bounds, 0.0f);

    if (build_node.vertex_count > 0) {
        node.first_vertex_index = build_node.first_vertex_index;
        node.vertex_count = build_node.vertex_count;
    } else {
        const auto left_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.left_child_index);

        const auto right_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.right_child_index);

        node.left_child_index = left_node_index;
        node.right_child_index = right_node_index;
    }
}

bool BodyVertexBvhData::is_valid(std::uint32_t vertex_count) const
{
    if (vertex_count == 0 ||
        nodes.empty() ||
        node_ranges_by_level.empty() ||
        root_node_index >= nodes.size() ||
        vertex_ids.size() != vertex_count) {
        return false;
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    const std::size_t node_count = nodes.size();
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        const BodyVertexBvhNode& node = nodes[node_index];
        if (is_leaf_node(node)) {
            if (!is_valid_leaf_node(node, vertex_count)) {
                return false;
            }
            for (std::uint32_t offset = 0; offset < node.vertex_count; ++offset) {
                const std::uint32_t vertex_id = vertex_ids[node.first_vertex_index + offset];
                if (vertex_id >= vertex_count || used_vertices[vertex_id] != 0u) {
                    return false;
                }
                used_vertices[vertex_id] = 1u;
            }
            continue;
        }

        if (!is_valid_internal_node(node, node_index, node_count)) {
            return false;
        }
    }

    return std::all_of(used_vertices.begin(), used_vertices.end(), [](std::uint8_t used) {
               return used != 0u;
           }) &&
           has_valid_shader_stack_depth(*this);
}
