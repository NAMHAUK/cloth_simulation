#include "gpu/bvh/VertexBvhBuilder.h"

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
}

VertexBvhBuilder::VertexBvhBuilder(std::uint32_t vertex_count,
                                   const std::vector<std::uint32_t>& triangle_indices,
                                   const std::vector<float>& vertices,
                                   const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices),
      triangle_part_labels_(triangle_part_labels)
{
}

VertexBvhData VertexBvhBuilder::build_vertex_bvh()
{
    VertexBvhData result;

    if (!build_vertex_items()) {
        return result;
    }

    const std::size_t leaf_count = (vertex_items_.size() + vertex_bvh_leaf_size - 1u) / vertex_bvh_leaf_size;
    build_nodes_.reserve(leaf_count * 2u - 1u);
    result.vertex_ids.reserve(vertex_items_.size());

    bvh_build::BvhBuildContext build_context{
        vertex_items_,
        build_nodes_,
        result.vertex_ids,
        vertex_bvh_leaf_size,
        has_part_labels()
    };
    const std::uint32_t source_root_node = bvh_build::build_bvh_tree(build_context, 0u, vertex_items_.size());
    bvh_build::write_level_ordered_bvh_data(source_root_node, build_nodes_, result.nodes, result.node_ranges_by_level);

    return result;
}

bool VertexBvhBuilder::build_vertex_items()
{
    if (vertex_count_ == 0 ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return false;
    }

    const bool use_part_labels = has_part_labels();
    std::vector<std::uint8_t> vertex_part_labels;
    if (use_part_labels && !build_vertex_part_labels(vertex_part_labels)) {
        return false;
    }

    vertex_items_.clear();
    vertex_items_.reserve(vertex_count_);

    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        const glm::vec3 position = get_vertex_position(vertices_, vertex_id);
        if (!bvh_build::is_valid_bounds(position, position)) {
            return false;
        }
        const std::uint8_t part_label = use_part_labels ? vertex_part_labels[vertex_id] : 0u;
        vertex_items_.push_back({vertex_id, position, position, position, part_label});
    }

    return true;
}

bool VertexBvhBuilder::has_part_labels() const
{
    return !triangle_part_labels_.empty();
}

bool VertexBvhBuilder::build_vertex_part_labels(std::vector<std::uint8_t>& vertex_part_labels) const
{
    if (source_triangle_indices_.size() % triangle_vertex_count != 0u) {
        return false;
    }

    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    if (triangle_part_labels_.size() != triangle_count) {
        return false;
    }

    using LabelCounts = std::array<std::uint32_t, bvh_build::character_part_label_count>;
    std::vector<LabelCounts> label_counts(vertex_count_, LabelCounts{});

    for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        if (part_label >= bvh_build::character_part_label_count) {
            return false;
        }

        const std::size_t index_base = triangle_index * triangle_vertex_count;
        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_id = source_triangle_indices_[index_base + index_offset];
            if (vertex_id >= vertex_count_) {
                return false;
            }

            ++label_counts[vertex_id][part_label];
        }
    }

    vertex_part_labels.resize(vertex_count_);
    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        const LabelCounts& counts = label_counts[vertex_id];
        const auto best_label = std::max_element(counts.begin(), counts.end()) - counts.begin();
        vertex_part_labels[vertex_id] = static_cast<std::uint8_t>(best_label);
    }

    return true;
}

bool VertexBvhData::is_valid(std::uint32_t vertex_count) const
{
    if (vertex_count == 0 ||
        nodes.empty() ||
        node_ranges_by_level.empty() ||
        vertex_ids.size() != vertex_count) {
        return false;
    }

    if (!bvh_build::has_valid_bvh_node_topology(nodes, vertex_count)) {
        return false;
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    for (const BvhNode& node : nodes) {
        if (bvh_build::is_leaf_node(node.element_count)) {
            for (std::uint32_t offset = 0; offset < node.element_count; ++offset) {
                const std::uint32_t vertex_id = vertex_ids[node.first_element_index + offset];
                if (vertex_id >= vertex_count || used_vertices[vertex_id] != 0u) {
                    return false;
                }
                used_vertices[vertex_id] = 1u;
            }
        }
    }

    return std::all_of(used_vertices.begin(), used_vertices.end(), [](std::uint8_t used) {
        return used != 0u;
    });
}
