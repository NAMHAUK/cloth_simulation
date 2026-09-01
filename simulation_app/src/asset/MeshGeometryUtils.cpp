#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
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

// bending constraint //
struct MeshEdgeGroup final
{
    std::vector<MeshEdge> edges;
    std::vector<std::uint8_t> used_vertices;
};

struct EdgeOppositeVertex final
{
    MeshEdge edge;
    std::uint32_t edge_opposite_vertex = 0;
};

struct TriangleEdgeUse final
{
    MeshEdge edge;
    std::uint32_t triangle_index = 0;
    bool follows_edge_order = false;
};

struct TriangleWindingNeighbor final
{
    std::uint32_t triangle_index = 0;
    bool requires_opposite_flip = false;
};

bool can_add_edge(const MeshEdgeGroup& group, const MeshEdge& edge)
{
    return group.used_vertices[edge.vertex_a] == 0u && group.used_vertices[edge.vertex_b] == 0u;
}

void add_edge(MeshEdgeGroup& group, const MeshEdge& edge)
{
    group.edges.push_back(edge);
    group.used_vertices[edge.vertex_a] = 1u;
    group.used_vertices[edge.vertex_b] = 1u;
}

void add_edge_opposite_vertex(std::vector<EdgeOppositeVertex>& edge_opposite_vertices,
                              std::uint32_t vertex_a,
                              std::uint32_t vertex_b,
                              std::uint32_t edge_opposite_vertex)
{
    if (vertex_a == vertex_b) {
        return;
    }

    edge_opposite_vertices.push_back({make_edge(vertex_a, vertex_b), edge_opposite_vertex});
}

void add_triangle_edge_use(std::vector<TriangleEdgeUse>& edge_uses,
                           std::uint32_t triangle_index,
                           std::uint32_t vertex_a,
                           std::uint32_t vertex_b)
{
    edge_uses.push_back({make_edge(vertex_a, vertex_b), triangle_index, vertex_a < vertex_b});
}

}

std::uint32_t orient_triangle_winding_outward(std::uint32_t vertex_count,
                                              const std::vector<float>& vertices,
                                              const glm::vec3& reference_point,
                                              std::vector<std::uint32_t>& triangle_indices)
{
    if (vertex_count == 0u ||
        vertices.size() != vertex_count * position_components ||
        triangle_indices.empty() ||
        triangle_indices.size() % 3u != 0u) {
        throw std::invalid_argument("Cannot orient triangle winding from invalid mesh data.");
    }

    const auto triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);
    std::vector<TriangleEdgeUse> edge_uses;
    edge_uses.reserve(triangle_indices.size());
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        const std::uint32_t vertex_a = triangle_indices[index_base];
        const std::uint32_t vertex_b = triangle_indices[index_base + 1u];
        const std::uint32_t vertex_c = triangle_indices[index_base + 2u];
        if (vertex_a >= vertex_count ||
            vertex_b >= vertex_count ||
            vertex_c >= vertex_count ||
            vertex_a == vertex_b ||
            vertex_b == vertex_c ||
            vertex_c == vertex_a) {
            throw std::invalid_argument("Cannot orient triangle winding from invalid mesh data.");
        }

        add_triangle_edge_use(edge_uses, triangle_index, vertex_a, vertex_b);
        add_triangle_edge_use(edge_uses, triangle_index, vertex_b, vertex_c);
        add_triangle_edge_use(edge_uses, triangle_index, vertex_c, vertex_a);
    }

    std::sort(edge_uses.begin(), edge_uses.end(), [](const TriangleEdgeUse& lhs, const TriangleEdgeUse& rhs) {
        if (lhs.edge == rhs.edge) {
            return lhs.triangle_index < rhs.triangle_index;
        }
        return lhs.edge < rhs.edge;
    });

    std::vector<std::vector<TriangleWindingNeighbor>> neighbors(triangle_count);
    for (std::size_t group_begin = 0u; group_begin < edge_uses.size();) {
        std::size_t group_end = group_begin + 1u;
        while (group_end < edge_uses.size() && edge_uses[group_begin].edge == edge_uses[group_end].edge) {
            ++group_end;
        }

        const std::size_t edge_use_count = group_end - group_begin;
        if (edge_use_count == 2u) {
            const TriangleEdgeUse& first = edge_uses[group_begin];
            const TriangleEdgeUse& second = edge_uses[group_begin + 1u];
            const bool requires_opposite_flip = first.follows_edge_order == second.follows_edge_order;
            neighbors[first.triangle_index].push_back({second.triangle_index, requires_opposite_flip});
            neighbors[second.triangle_index].push_back({first.triangle_index, requires_opposite_flip});
        }

        group_begin = group_end;
    }

    std::vector<std::int8_t> should_flip(triangle_count, -1);
    std::vector<std::uint32_t> stack;
    std::vector<std::uint32_t> component_triangles;
    for (std::uint32_t root_triangle = 0u; root_triangle < triangle_count; ++root_triangle) {
        if (should_flip[root_triangle] >= 0) {
            continue;
        }

        should_flip[root_triangle] = 0;
        stack.push_back(root_triangle);
        component_triangles.clear();
        while (!stack.empty()) {
            const std::uint32_t triangle_index = stack.back();
            stack.pop_back();
            component_triangles.push_back(triangle_index);

            for (const TriangleWindingNeighbor& neighbor : neighbors[triangle_index]) {
                const std::int8_t required_flip = static_cast<std::int8_t>(
                    should_flip[triangle_index] ^ (neighbor.requires_opposite_flip ? 1 : 0));
                if (should_flip[neighbor.triangle_index] < 0) {
                    should_flip[neighbor.triangle_index] = required_flip;
                    stack.push_back(neighbor.triangle_index);
                } else if (should_flip[neighbor.triangle_index] != required_flip) {
                    throw std::runtime_error("Cannot orient triangle winding consistently.");
                }
            }
        }

        double orientation_score = 0.0;
        for (std::uint32_t triangle_index : component_triangles) {
            const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
            const glm::vec3 position_a = get_vertex_position(vertices, triangle_indices[index_base]);
            glm::vec3 position_b = get_vertex_position(vertices, triangle_indices[index_base + 1u]);
            glm::vec3 position_c = get_vertex_position(vertices, triangle_indices[index_base + 2u]);
            if (should_flip[triangle_index] != 0) {
                std::swap(position_b, position_c);
            }

            const glm::vec3 area_normal = glm::cross(position_b - position_a, position_c - position_a);
            const glm::vec3 centroid = (position_a + position_b + position_c) / 3.0f;
            orientation_score += static_cast<double>(glm::dot(area_normal, centroid - reference_point));
        }

        if (orientation_score < 0.0) {
            for (std::uint32_t triangle_index : component_triangles) {
                should_flip[triangle_index] ^= 1;
            }
        }
    }

    std::uint32_t flipped_triangle_count = 0u;
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        if (should_flip[triangle_index] == 0) {
            continue;
        }

        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        std::swap(triangle_indices[index_base + 1u], triangle_indices[index_base + 2u]);
        ++flipped_triangle_count;
    }
    return flipped_triangle_count;
}

// vertex position //
glm::vec3 get_vertex_position(const std::vector<float>& vertices, std::uint32_t vertex_index)
{
    const std::size_t position_base = vertex_index * position_components;
    return {
        vertices[position_base],
        vertices[position_base + 1u],
        vertices[position_base + 2u],
    };
}

// vertex_triangle_adjacency //
// 각 vertex가 어떤 triangle들에 포함되는지 계산
VertexTriangleAdjacency build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                                        const std::vector<std::uint32_t>& triangle_indices)
{
    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        throw std::invalid_argument("Cannot build vertex-triangle adjacency from invalid topology.");
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);
    VertexTriangleAdjacency adjacency;
    adjacency.offsets.resize(static_cast<std::size_t>(vertex_count) + 1u, 0);

    // 각 vetex가 전체 triangle에서 몇 번 나오는지 count
    for (std::uint32_t index : triangle_indices) {
        if (index >= vertex_count) {
            throw std::invalid_argument("Cannot build vertex-triangle adjacency from invalid topology.");
        }
        ++adjacency.offsets[static_cast<std::size_t>(index) + 1u];
    }

    // offsets에 vertex 누적합으로 저장 -> 각 vertex의 triangle_indices 시작 위치
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const std::size_t offset_index = static_cast<std::size_t>(vertex_index);
        const std::size_t next_offset_index = offset_index + 1u;
        adjacency.offsets[next_offset_index] += adjacency.offsets[offset_index];
    }

    // 각 vertex가 포함된 triangle index 저장 (여러 triangle에 포함된 경우 연속되게 저장됨)
    adjacency.triangle_indices.resize(adjacency.offsets.back(), 0);
    std::vector<std::uint32_t> write_offsets = adjacency.offsets;

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        for (std::uint32_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertex_index = triangle_indices[index_base + corner];
            const std::uint32_t write_index = write_offsets[vertex_index]++;
            adjacency.triangle_indices[write_index] = triangle_index;
        }
    }

    adjacency.triangle_count = triangle_count;
    return adjacency;
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

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

// bending constraint //
std::vector<MeshEdge> build_unique_bending_edges(std::uint32_t vertex_count,
                                                 const std::vector<std::uint32_t>& triangle_indices)
{
    std::vector<EdgeOppositeVertex> edge_opposite_vertices;
    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return {};
    }

    edge_opposite_vertices.reserve(triangle_indices.size());
    for (std::size_t index = 0; index < triangle_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = triangle_indices[index];
        const std::uint32_t vertex_b = triangle_indices[index + 1u];
        const std::uint32_t vertex_c = triangle_indices[index + 2u];

        if (vertex_a >= vertex_count || vertex_b >= vertex_count || vertex_c >= vertex_count) {
            return {};
        }

        add_edge_opposite_vertex(edge_opposite_vertices, vertex_a, vertex_b, vertex_c);
        add_edge_opposite_vertex(edge_opposite_vertices, vertex_b, vertex_c, vertex_a);
        add_edge_opposite_vertex(edge_opposite_vertices, vertex_c, vertex_a, vertex_b);
    }

    std::sort(
        edge_opposite_vertices.begin(),
        edge_opposite_vertices.end(),
        [](const EdgeOppositeVertex& lhs, const EdgeOppositeVertex& rhs) { return lhs.edge < rhs.edge; });

    std::vector<MeshEdge> bending_edges;
    for (std::size_t group_begin = 0; group_begin < edge_opposite_vertices.size();) {
        std::size_t group_end = group_begin + 1u;
        while (group_end < edge_opposite_vertices.size() &&
               edge_opposite_vertices[group_begin].edge == edge_opposite_vertices[group_end].edge) {
            ++group_end;
        }

        if (group_end - group_begin == 2u) {
            const std::uint32_t edge_opposite_vertex_a =
                edge_opposite_vertices[group_begin].edge_opposite_vertex;
            const std::uint32_t edge_opposite_vertex_b =
                edge_opposite_vertices[group_begin + 1u].edge_opposite_vertex;
            if (edge_opposite_vertex_a != edge_opposite_vertex_b) {
                bending_edges.push_back(make_edge(edge_opposite_vertex_a, edge_opposite_vertex_b));
            }
        }

        group_begin = group_end;
    }

    std::sort(bending_edges.begin(), bending_edges.end());
    bending_edges.erase(std::unique(bending_edges.begin(), bending_edges.end()), bending_edges.end());
    return bending_edges;
}

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count, const std::vector<MeshEdge>& edges)
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
    colorized_edges.color_states.reserve(edge_groups.size());
    for (const MeshEdgeGroup& edge_group : edge_groups) {
        if (edge_group.edges.empty()) {
            continue;
        }

        ConstraintColorState color_state;
        color_state.start_index = static_cast<std::uint32_t>(colorized_edges.edges.size());
        color_state.count = static_cast<std::uint32_t>(edge_group.edges.size());
        colorized_edges.color_states.push_back(color_state);

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
        const glm::vec3 vertex_a = get_vertex_position(vertices, edge.vertex_a);
        const glm::vec3 vertex_b = get_vertex_position(vertices, edge.vertex_b);
        lengths.push_back(glm::length(vertex_b - vertex_a));
    }
    return lengths;
}
