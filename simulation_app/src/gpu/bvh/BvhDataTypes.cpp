#include "gpu/bvh/BvhDataTypes.h"

#include "gpu/bvh/BvhBuildUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr std::uint32_t edge_vertex_count = 2;
constexpr std::uint32_t triangle_vertex_count = 3;
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
