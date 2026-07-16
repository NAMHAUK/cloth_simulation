#include "gpu/bvh/EdgeBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include <glm/glm.hpp>

namespace {
constexpr std::uint32_t edge_vertex_count = 2;
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t vertex_position_component_count = 3;
constexpr std::uint32_t edge_bvh_leaf_size = 8;

MeshEdge make_ordered_edge(std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    if (vertex_a < vertex_b) {
        return {vertex_a, vertex_b};
    }
    return {vertex_b, vertex_a};
}

std::uint64_t edge_key(MeshEdge edge)
{
    return (static_cast<std::uint64_t>(edge.vertex_a) << 32u) | edge.vertex_b;
}
}

EdgeBvhBuilder::EdgeBvhBuilder(std::uint32_t vertex_count,
                               const std::vector<std::uint32_t>& triangle_indices,
                               const std::vector<float>& vertices,
                               const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices),
      triangle_part_labels_(triangle_part_labels)
{
}

EdgeBvhData EdgeBvhBuilder::build_edge_bvh()
{
    EdgeBvhData result;

    if (!build_edge_items()) {
        return result;
    }

    const std::size_t leaf_count = (edge_items_.size() + edge_bvh_leaf_size - 1u) / edge_bvh_leaf_size;
    build_nodes_.reserve(leaf_count * 2u - 1u);

    std::vector<std::uint32_t> ordered_edge_indices;
    ordered_edge_indices.reserve(edge_items_.size());

    bvh_build::BvhBuildContext build_context{
        edge_items_,
        build_nodes_,
        ordered_edge_indices,
        edge_bvh_leaf_size,
        has_part_labels()
    };
    const std::uint32_t source_root_node = bvh_build::build_bvh_tree(build_context, 0u, edge_items_.size());
    write_edge_index_payload(ordered_edge_indices, result.edge_vertex_indices);
    bvh_build::write_level_ordered_bvh_data(source_root_node, build_nodes_, result.nodes, result.node_ranges_by_level);
    return result;
}

bool EdgeBvhBuilder::build_edge_items()
{
    if (vertex_count_ == 0 ||
        source_triangle_indices_.empty() ||
        source_triangle_indices_.size() % triangle_vertex_count != 0u ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return false;
    }

    source_edges_ = build_unique_triangle_edges(vertex_count_, source_triangle_indices_);
    if (source_edges_.empty()) {
        return false;
    }

    std::vector<std::uint8_t> edge_part_labels;
    if (has_part_labels() && !build_edge_part_labels(edge_part_labels)) {
        return false;
    }

    edge_items_.clear();
    edge_items_.reserve(source_edges_.size());

    for (std::uint32_t edge_index = 0; edge_index < source_edges_.size(); ++edge_index) {
        const MeshEdge edge = source_edges_[edge_index];
        if (edge.vertex_a >= vertex_count_ || edge.vertex_b >= vertex_count_ || edge.vertex_a == edge.vertex_b) {
            return false;
        }

        const glm::vec3 position_a = get_vertex_position(vertices_, edge.vertex_a);
        const glm::vec3 position_b = get_vertex_position(vertices_, edge.vertex_b);
        const glm::vec3 min_bounds = glm::min(position_a, position_b);
        const glm::vec3 max_bounds = glm::max(position_a, position_b);
        if (!bvh_build::is_valid_bounds(min_bounds, max_bounds)) {
            return false;
        }

        const std::uint8_t part_label = has_part_labels() ? edge_part_labels[edge_index] : 0u;
        edge_items_.push_back({
            edge_index,
            (position_a + position_b) * 0.5f,
            min_bounds,
            max_bounds,
            part_label
        });
    }

    return true;
}

bool EdgeBvhBuilder::has_part_labels() const
{
    return !triangle_part_labels_.empty();
}

bool EdgeBvhBuilder::build_edge_part_labels(std::vector<std::uint8_t>& edge_part_labels) const
{
    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    if (triangle_part_labels_.size() != triangle_count) {
        return false;
    }

    using LabelCounts = std::array<std::uint32_t, bvh_build::character_part_label_count>;
    std::vector<LabelCounts> label_counts(source_edges_.size(), LabelCounts{});
    std::unordered_map<std::uint64_t, std::uint32_t> edge_indices_by_key;
    edge_indices_by_key.reserve(source_edges_.size());
    for (std::uint32_t edge_index = 0; edge_index < source_edges_.size(); ++edge_index) {
        edge_indices_by_key.emplace(edge_key(source_edges_[edge_index]), edge_index);
    }

    for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        if (part_label >= bvh_build::character_part_label_count) {
            return false;
        }

        const std::size_t index_base = triangle_index * triangle_vertex_count;
        const std::uint32_t vertex_a = source_triangle_indices_[index_base];
        const std::uint32_t vertex_b = source_triangle_indices_[index_base + 1u];
        const std::uint32_t vertex_c = source_triangle_indices_[index_base + 2u];
        if (vertex_a >= vertex_count_ || vertex_b >= vertex_count_ || vertex_c >= vertex_count_) {
            return false;
        }

        const MeshEdge triangle_edges[triangle_vertex_count] = {
            make_ordered_edge(vertex_a, vertex_b),
            make_ordered_edge(vertex_b, vertex_c),
            make_ordered_edge(vertex_c, vertex_a),
        };
        for (const MeshEdge edge : triangle_edges) {
            if (edge.vertex_a == edge.vertex_b) {
                continue;
            }
            const auto iter = edge_indices_by_key.find(edge_key(edge));
            if (iter == edge_indices_by_key.end()) {
                return false;
            }
            ++label_counts[iter->second][part_label];
        }
    }

    edge_part_labels.resize(source_edges_.size());
    for (std::uint32_t edge_index = 0; edge_index < source_edges_.size(); ++edge_index) {
        const LabelCounts& counts = label_counts[edge_index];
        const auto best_label = std::max_element(counts.begin(), counts.end()) - counts.begin();
        if (counts[best_label] == 0u) {
            return false;
        }
        edge_part_labels[edge_index] = static_cast<std::uint8_t>(best_label);
    }

    return true;
}

void EdgeBvhBuilder::write_edge_index_payload(const std::vector<std::uint32_t>& ordered_edge_indices,
                                              std::vector<std::uint32_t>& edge_vertex_indices) const
{
    edge_vertex_indices.reserve(ordered_edge_indices.size() * edge_vertex_count);
    for (const std::uint32_t edge_index : ordered_edge_indices) {
        const MeshEdge edge = source_edges_[edge_index];
        edge_vertex_indices.push_back(edge.vertex_a);
        edge_vertex_indices.push_back(edge.vertex_b);
    }
}

std::uint32_t EdgeBvhData::edge_count() const
{
    return static_cast<std::uint32_t>(edge_vertex_indices.size() / edge_vertex_count);
}

bool EdgeBvhData::is_valid() const
{
    if (edge_vertex_indices.empty() ||
        edge_vertex_indices.size() % edge_vertex_count != 0u ||
        nodes.empty() ||
        node_ranges_by_level.empty()) {
        return false;
    }

    const std::uint32_t source_edge_count = edge_count();
    if (!bvh_build::has_valid_bvh_node_topology(nodes, source_edge_count)) {
        return false;
    }

    std::vector<std::uint8_t> used_edges(source_edge_count, 0u);
    for (const BvhNode& node : nodes) {
        if (!bvh_build::is_leaf_node(node.element_count)) {
            continue;
        }

        for (std::uint32_t offset = 0; offset < node.element_count; ++offset) {
            const std::uint32_t edge_index = node.first_element_index + offset;
            if (edge_index >= source_edge_count || used_edges[edge_index] != 0u) {
                return false;
            }
            used_edges[edge_index] = 1u;
            if (edge_vertex_indices[edge_index * edge_vertex_count] ==
                edge_vertex_indices[edge_index * edge_vertex_count + 1u]) {
                return false;
            }
        }
    }

    return std::all_of(used_edges.begin(), used_edges.end(), [](std::uint8_t used) {
        return used != 0u;
    });
}
