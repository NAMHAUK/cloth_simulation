#include "gpu/bvh/TriangleBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t triangle_bvh_leaf_size = 8;
}

TriangleBvhBuilder::TriangleBvhBuilder(std::uint32_t vertex_count,
                               const std::vector<std::uint32_t>& triangle_indices,
                               const std::vector<float>& vertices)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices)
{
}

TriangleBvhBuilder::TriangleBvhBuilder(std::uint32_t vertex_count,
                               const std::vector<std::uint32_t>& triangle_indices,
                               const std::vector<float>& vertices,
                               const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices),
      triangle_part_labels_(&triangle_part_labels)
{
}

TriangleBvhData TriangleBvhBuilder::build_triangle_bvh()
{
    TriangleBvhData result;

    if (!build_triangle_items()) {
        return result;
    }

    const std::size_t leaf_count = (triangle_items_.size() + triangle_bvh_leaf_size - 1u) / triangle_bvh_leaf_size;
    build_nodes_.reserve(leaf_count * 2u - 1u);
    result.triangle_indices.reserve(source_triangle_indices_.size());

    std::vector<std::uint32_t> ordered_triangle_indices;
    ordered_triangle_indices.reserve(triangle_items_.size());

    bvh_build::BvhBuildContext build_context{
        triangle_items_,
        build_nodes_,
        ordered_triangle_indices,
        triangle_bvh_leaf_size,
        has_part_labels()
    };
    const std::uint32_t source_root_node = bvh_build::build_bvh_tree(build_context, 0u, triangle_items_.size());
    write_triangle_index_payload(ordered_triangle_indices, result.triangle_indices);
    
    bvh_build::write_level_ordered_bvh_data(source_root_node, build_nodes_, result.nodes, result.node_ranges_by_level);
    return result;
}

bool TriangleBvhBuilder::build_triangle_items()
{
    if (vertex_count_ == 0 ||
        source_triangle_indices_.empty() ||
        source_triangle_indices_.size() % triangle_vertex_count != 0u ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * triangle_vertex_count) {
        return false;
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(source_triangle_indices_.size() / triangle_vertex_count);
    if (has_part_labels() && triangle_part_labels_->size() != triangle_count) {
        return false;
    }

    triangle_items_.clear();
    triangle_items_.reserve(triangle_count);

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        
        glm::vec3 center_sum(0.0f);
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());

        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = source_triangle_indices_[index_base + index_offset];
            if (vertex_index >= vertex_count_) {
                return false;
            }

            const glm::vec3 position = get_vertex_position(vertices_, vertex_index);
            center_sum += position;
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }

        if (!bvh_build::is_valid_bounds(min_bounds, max_bounds)) {
            return false;
        }

        triangle_items_.push_back({
            triangle_index,
            center_sum / static_cast<float>(triangle_vertex_count),
            min_bounds,
            max_bounds,
            has_part_labels() ? (*triangle_part_labels_)[triangle_index] : 0u
        });
    }

    return true;
}

bool TriangleBvhBuilder::has_part_labels() const
{
    return triangle_part_labels_ != nullptr && !triangle_part_labels_->empty();
}

void TriangleBvhBuilder::write_triangle_index_payload(const std::vector<std::uint32_t>& ordered_triangle_indices,
                                                      std::vector<std::uint32_t>& triangle_indices) const
{
    for (const std::uint32_t triangle_index : ordered_triangle_indices) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        triangle_indices.push_back(source_triangle_indices_[index_base]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 1u]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 2u]);
    }
}

bool TriangleBvhData::is_valid(std::uint32_t triangle_count) const
{
    if (triangle_count == 0 ||
        nodes.empty() ||
        node_ranges_by_level.empty() ||
        triangle_indices.size() != static_cast<std::size_t>(triangle_count) * triangle_vertex_count) {
        return false;
    }

    return bvh_build::has_valid_bvh_node_topology(nodes, triangle_count);
}
