#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
// stretch constraint //
MeshEdge make_edge(std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    if (vertex_a < vertex_b) {
        return {vertex_a, vertex_b};
    }
    return {vertex_b, vertex_a};
}

bool is_less_edge(const MeshEdge& lhs, const MeshEdge& rhs)
{
    if (lhs.vertex_a != rhs.vertex_a) {
        return lhs.vertex_a < rhs.vertex_a;
    }
    return lhs.vertex_b < rhs.vertex_b;
}

bool is_same_mesh_edge(const MeshEdge& lhs, const MeshEdge& rhs)
{
    return lhs.vertex_a == rhs.vertex_a && lhs.vertex_b == rhs.vertex_b;
}

struct MeshEdgeGroup final {
    std::vector<MeshEdge> edges;
    std::vector<std::uint8_t> used_vertices;
};

bool can_add_edge(const MeshEdgeGroup& group, const MeshEdge& edge)
{
    return group.used_vertices[edge.vertex_a] == 0u &&
           group.used_vertices[edge.vertex_b] == 0u;
}

void add_edge(MeshEdgeGroup& group, const MeshEdge& edge)
{
    group.edges.push_back(edge);
    group.used_vertices[edge.vertex_a] = 1u;
    group.used_vertices[edge.vertex_b] = 1u;
}

// rest length //
constexpr std::uint32_t position_components = 3;

glm::vec3 read_vertex_position(const std::vector<float>& vertices, std::uint32_t vertex_index)
{
    const std::size_t position_base = static_cast<std::size_t>(vertex_index) * position_components;
    return {
        vertices[position_base],
        vertices[position_base + 1u],
        vertices[position_base + 2u],
    };
}
}

// vertex_face_adjacency //
bool VertexFaceAdjacency::is_valid(std::uint32_t vertex_count) const
{
    return face_count > 0 &&
           offsets.size() == static_cast<std::size_t>(vertex_count) + 1u &&
           !face_indices.empty();
}

// 각 vertex가 어떤 face들에 포함되는지 계산
bool build_vertex_face_adjacency(std::uint32_t vertex_count,
                                 const std::vector<std::uint32_t>& triangle_indices,
                                 VertexFaceAdjacency& adjacency)
{
    adjacency = {};

    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return false;
    }

    const std::uint32_t face_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);
    adjacency.offsets.resize(static_cast<std::size_t>(vertex_count) + 1u, 0);

    // 각 vetex가 전체 face에서 몇 번 나오는지 count
    for (std::uint32_t index : triangle_indices) {
        if (index >= vertex_count) {
            return false;
        }
        ++adjacency.offsets[static_cast<std::size_t>(index) + 1u];
    }

    // offsets에 vertex 누적합으로 저장 -> 각 vertex의 face_indices 시작 위치
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const std::size_t offset_index = static_cast<std::size_t>(vertex_index);
        const std::size_t next_offset_index = offset_index + 1u;
        adjacency.offsets[next_offset_index] += adjacency.offsets[offset_index];
    }

    // 각 vertex가 포함된 face index 저장 (여러 face에 포함된 경우 연속되게 저장됨)
    adjacency.face_indices.resize(adjacency.offsets.back(), 0);
    std::vector<std::uint32_t> write_offsets = adjacency.offsets;

    for (std::uint32_t face_index = 0; face_index < face_count; ++face_index) {
        const std::size_t index_base = static_cast<std::size_t>(face_index) * 3u;
        for (std::uint32_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertex_index = triangle_indices[index_base + corner];
            const std::uint32_t write_index = write_offsets[vertex_index]++;
            adjacency.face_indices[write_index] = face_index;
        }
    }

    adjacency.face_count = face_count;
    return adjacency.is_valid(vertex_count);
}

// stretch constraint //
std::vector<MeshEdge> build_unique_triangle_edges(std::uint32_t vertex_count,
                                                  const std::vector<std::uint32_t>& triangle_indices)
{
    std::vector<MeshEdge> edges;
    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return edges;
    }

    edges.reserve(triangle_indices.size());
    for (std::size_t index = 0; index < triangle_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = triangle_indices[index];
        const std::uint32_t vertex_b = triangle_indices[index + 1u];
        const std::uint32_t vertex_c = triangle_indices[index + 2u];

        if (vertex_a >= vertex_count || vertex_b >= vertex_count || vertex_c >= vertex_count) {
            return {};
        }

        if (vertex_a != vertex_b) {
            edges.push_back(make_edge(vertex_a, vertex_b));
        }
        if (vertex_b != vertex_c) {
            edges.push_back(make_edge(vertex_b, vertex_c));
        }
        if (vertex_c != vertex_a) {
            edges.push_back(make_edge(vertex_c, vertex_a));
        }
    }

    std::sort(edges.begin(), edges.end(), is_less_edge);
    edges.erase(std::unique(edges.begin(), edges.end(), is_same_mesh_edge), edges.end());
    return edges;
}

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count,
                                       const std::vector<MeshEdge>& edges)
{
    std::vector<MeshEdgeGroup> edge_groups;

    for (const MeshEdge& edge : edges) {
        if (edge.vertex_a >= vertex_count || edge.vertex_b >= vertex_count) {
            return {};
        }

        bool inserted = false;
        for (MeshEdgeGroup& edge_group : edge_groups) {
            if (!can_add_edge(edge_group, edge)) {
                continue;
            }

            add_edge(edge_group, edge);
            inserted = true;
            break;
        }

        if (!inserted) {
            MeshEdgeGroup edge_group;
            edge_group.used_vertices.resize(vertex_count, 0u);
            add_edge(edge_group, edge);
            edge_groups.push_back(std::move(edge_group));
        }
    }

    ColorizedMeshEdges colorized_edges;
    colorized_edges.edges.reserve(edges.size());
    colorized_edges.ranges.reserve(edge_groups.size());
    for (const MeshEdgeGroup& edge_group : edge_groups) {
        if (edge_group.edges.empty()) {
            continue;
        }

        MeshEdgeRange range;
        range.offset = static_cast<std::uint32_t>(colorized_edges.edges.size());
        range.count = static_cast<std::uint32_t>(edge_group.edges.size());
        colorized_edges.ranges.push_back(range);

        colorized_edges.edges.insert(colorized_edges.edges.end(),
                                     edge_group.edges.begin(),
                                     edge_group.edges.end());
    }

    return colorized_edges;
}

// rest length //
std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<float>& vertices)
{
    std::vector<float> lengths;
    lengths.reserve(edges.size());
    for (const MeshEdge& edge : edges) {
        const glm::vec3 vertex_a = read_vertex_position(vertices, edge.vertex_a);
        const glm::vec3 vertex_b = read_vertex_position(vertices, edge.vertex_b);
        lengths.push_back(glm::length(vertex_b - vertex_a));
    }
    return lengths;
}
