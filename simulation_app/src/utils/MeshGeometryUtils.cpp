#include "utils/MeshGeometryUtils.h"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr std::size_t max_color_count = 64u;

MeshEdge make_edge(std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    if (vertex_a < vertex_b) {
        return {vertex_a, vertex_b};
    }
    return {vertex_b, vertex_a};
}

struct EdgeOppositeVertex final
{
    MeshEdge edge;
    std::uint32_t edge_opposite_vertex = 0;
};

std::size_t assign_edge_color(std::vector<std::bitset<max_color_count>>& assigned_colors,
                              const MeshEdge& edge)
{
    const auto unavailable_colors = assigned_colors[edge.vertex_a] | assigned_colors[edge.vertex_b];

    for (std::size_t color_index = 0; color_index < max_color_count; ++color_index) {
        if (unavailable_colors.test(color_index)) {
            continue;
        }

        assigned_colors[edge.vertex_a].set(color_index);
        assigned_colors[edge.vertex_b].set(color_index);
        return color_index;
    }

    throw std::runtime_error("Garment requires more than 64 constraint colors.");
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

struct TriangleEdge final
{
    std::uint32_t triangle_index = 0;
    std::uint32_t start_vertex_index = 0;
    bool has_neighbor = false;
};

struct TriangleNeighbor final
{
    std::uint32_t triangle_index = 0;
    bool has_winding_conflict = false;
};

bool determine_neighbor_flip(bool current_needs_flip, bool has_winding_conflict)
{
    return has_winding_conflict ? !current_needs_flip : current_needs_flip;
}

void add_triangle_edge(std::map<MeshEdge, TriangleEdge>& triangle_edge_map,
                       std::vector<std::vector<TriangleNeighbor>>& triangle_neighbors,
                       std::uint32_t triangle_index,
                       std::uint32_t vertex_a,
                       std::uint32_t vertex_b)
{
    const MeshEdge edge = make_edge(vertex_a, vertex_b);
    const bool is_first_check_edge = triangle_edge_map.count(edge) == 0u;
    TriangleEdge& triangle_edge = triangle_edge_map[edge];

    if (is_first_check_edge) {
        triangle_edge = {triangle_index, vertex_a};
    } else if (!triangle_edge.has_neighbor) {
        const bool has_winding_conflict = vertex_a == triangle_edge.start_vertex_index;
        triangle_neighbors[triangle_index].push_back({triangle_edge.triangle_index, has_winding_conflict});
        triangle_neighbors[triangle_edge.triangle_index].push_back({triangle_index, has_winding_conflict});
        triangle_edge.has_neighbor = true;
    } else {
        throw std::runtime_error("Garment edge is shared by more than two triangles.");
    }
}
}

glm::vec3 get_vertex_position(const std::vector<float>& vertices, std::uint32_t vertex_index)
{
    const std::size_t position_base = vertex_index * position_components;
    return {
        vertices[position_base],
        vertices[position_base + 1u],
        vertices[position_base + 2u],
    };
}

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

// garment converter utils

void orient_triangles_outward(const std::vector<float>& vertices,
                              const glm::vec3& bounds_center,
                              std::vector<std::uint32_t>& triangle_indices)
{
    const auto triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);

    // Build triangle edge map and neighbors
    std::map<MeshEdge, TriangleEdge> triangle_edge_map;
    std::vector<std::vector<TriangleNeighbor>> triangle_neighbors(triangle_count);
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        const std::uint32_t vertex_a = triangle_indices[triangle_index * 3u];
        const std::uint32_t vertex_b = triangle_indices[triangle_index * 3u + 1u];
        const std::uint32_t vertex_c = triangle_indices[triangle_index * 3u + 2u];

        add_triangle_edge(triangle_edge_map, triangle_neighbors, triangle_index, vertex_a, vertex_b);
        add_triangle_edge(triangle_edge_map, triangle_neighbors, triangle_index, vertex_b, vertex_c);
        add_triangle_edge(triangle_edge_map, triangle_neighbors, triangle_index, vertex_c, vertex_a);
    }

    // Align triangle directions
    std::vector<std::optional<bool>> flip_decisions(triangle_count);
    std::queue<std::uint32_t> triangle_queue;
    flip_decisions[0] = false;
    triangle_queue.push(0u);

    while (!triangle_queue.empty()) {
        const std::uint32_t triangle_index = triangle_queue.front();
        triangle_queue.pop();

        for (const TriangleNeighbor& neighbor : triangle_neighbors[triangle_index]) {
            const bool neighbor_needs_flip = determine_neighbor_flip(flip_decisions[triangle_index].value(),
                                                                     neighbor.has_winding_conflict);
            
            if (!flip_decisions[neighbor.triangle_index].has_value()) {
                flip_decisions[neighbor.triangle_index] = neighbor_needs_flip;
                triangle_queue.push(neighbor.triangle_index);
            } else if (flip_decisions[neighbor.triangle_index].value() != neighbor_needs_flip) {
                throw std::runtime_error("Conflicting triangle flip decisions.");
            }
        }
    }

    if (std::find(flip_decisions.begin(), flip_decisions.end(), std::nullopt) != flip_decisions.end()) {
        throw std::runtime_error("Garment triangles must form one edge-connected component.");
    }

    // Orient outward
    double orientation_score = 0.0;
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        const glm::vec3 position_a = get_vertex_position(vertices, triangle_indices[triangle_index * 3u]);
        const glm::vec3 position_b = get_vertex_position(vertices, triangle_indices[triangle_index * 3u + 1u]);
        const glm::vec3 position_c = get_vertex_position(vertices, triangle_indices[triangle_index * 3u + 2u]);
        const glm::vec3 area_normal = glm::cross(position_b - position_a, position_c - position_a);
        const glm::vec3 centroid = (position_a + position_b + position_c) / 3.0f;
        
        const double triangle_score = static_cast<double>(glm::dot(area_normal, centroid - bounds_center));
        orientation_score += flip_decisions[triangle_index].value() ? -triangle_score : triangle_score;
    }

    if (orientation_score < 0.0) {
        for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            flip_decisions[triangle_index] = !flip_decisions[triangle_index].value();
        }
    }

    // Flip triangles
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        if (!flip_decisions[triangle_index].value()) {
            continue;
        }

        std::swap(triangle_indices[triangle_index * 3u + 1u], triangle_indices[triangle_index * 3u + 2u]);
    }
}

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count, const std::vector<MeshEdge>& edges)
{
    std::vector<std::vector<MeshEdge>> edge_groups;
    std::vector<std::bitset<max_color_count>> assigned_colors(vertex_count);

    for (const MeshEdge& edge : edges) {
        const std::size_t color_index = assign_edge_color(assigned_colors, edge);
        if (color_index == edge_groups.size()) {
            edge_groups.emplace_back();
        }
        edge_groups[color_index].push_back(edge);
    }

    ColorizedMeshEdges colorized_edges;
    colorized_edges.edges.reserve(edges.size());
    colorized_edges.color_states.reserve(edge_groups.size());

    for (const std::vector<MeshEdge>& edge_group : edge_groups) {
        colorized_edges.color_states.push_back({static_cast<std::uint32_t>(colorized_edges.edges.size()),
                                                static_cast<std::uint32_t>(edge_group.size())});

        colorized_edges.edges.insert(colorized_edges.edges.end(), edge_group.begin(), edge_group.end());
    }

    return colorized_edges;
}
