#include "gpu/body/bvh/VertexBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t vertex_position_component_count = 3;
constexpr std::uint32_t vertex_bvh_leaf_size = 8;
constexpr std::size_t shader_max_bvh_stack_depth = 32u;

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
            position
        });
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
