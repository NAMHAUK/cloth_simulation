#include "utils/MeshGeometryUtils.h"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

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

VertexTriangleAdjacency build_vertex_triangle_adjacency(
    std::uint32_t vertex_count,
    const std::vector<TriangleVertexIndices>& vertex_indices)
{
    if (vertex_count == 0 || vertex_indices.empty()) {
        throw std::invalid_argument("Cannot build vertex-triangle adjacency from invalid topology.");
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(vertex_indices.size());
    VertexTriangleAdjacency adjacency;
    adjacency.offsets.resize(static_cast<std::size_t>(vertex_count) + 1u, 0);

    // 각 vetex가 전체 triangle에서 몇 번 나오는지 count
    for (const TriangleVertexIndices& triangle : vertex_indices) {
        for (std::uint32_t index : {triangle.a, triangle.b, triangle.c}) {
            if (index >= vertex_count) {
                throw std::invalid_argument("Cannot build vertex-triangle adjacency from invalid topology.");
            }
            ++adjacency.offsets[static_cast<std::size_t>(index) + 1u];
        }
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
        const TriangleVertexIndices& triangle = vertex_indices[triangle_index];
        for (std::uint32_t vertex_index : {triangle.a, triangle.b, triangle.c}) {
            const std::uint32_t write_index = write_offsets[vertex_index]++;
            adjacency.triangle_indices[write_index] = triangle_index;
        }
    }

    adjacency.triangle_count = triangle_count;
    return adjacency;
}

std::vector<std::uint32_t> build_vertex_neighborhood_masks(
    std::uint32_t vertex_count,
    const std::vector<TriangleVertexIndices>& vertex_indices)
{
    const auto adjacency = build_vertex_triangle_adjacency(vertex_count, vertex_indices);
    const std::size_t masks_per_vertex = (static_cast<std::size_t>(adjacency.triangle_count) + 31u) / 32u;
    std::vector<std::uint32_t> exclusions(vertex_count * masks_per_vertex, 0u);

    for (std::uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
        const std::size_t row_offset = vertex * masks_per_vertex;
        for (std::uint32_t entry = adjacency.offsets[vertex]; entry < adjacency.offsets[vertex + 1u]; ++entry) {
            const TriangleVertexIndices& adjacent_triangle = vertex_indices[adjacency.triangle_indices[entry]];
            for (std::uint32_t neighbor : {adjacent_triangle.a, adjacent_triangle.b, adjacent_triangle.c}) {
                for (std::uint32_t neighbor_entry = adjacency.offsets[neighbor]; neighbor_entry < adjacency.offsets[neighbor + 1u]; ++neighbor_entry) {
                    const std::uint32_t triangle = adjacency.triangle_indices[neighbor_entry];
                    exclusions[row_offset + triangle / 32u] |= 1u << (triangle % 32u);
                }
            }
        }
    }
    return exclusions;
}

std::vector<MeshEdge> build_unique_triangle_edges(const std::vector<TriangleVertexIndices>& vertex_indices)
{
    std::vector<MeshEdge> edges;
    edges.reserve(vertex_indices.size() * 3u);
    for (const auto& [vertex_a, vertex_b, vertex_c] : vertex_indices) {

        edges.push_back(make_edge(vertex_a, vertex_b));
        edges.push_back(make_edge(vertex_b, vertex_c));
        edges.push_back(make_edge(vertex_c, vertex_a));
    }

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

std::vector<std::uint32_t> build_edge_exclusions(std::uint32_t vertex_count,
                                                 const std::vector<MeshEdge>& edges)
{
    const auto edge_count = static_cast<std::uint32_t>(edges.size());
    std::vector<std::vector<std::uint32_t>> vertex_edges(vertex_count);
    for (std::uint32_t edge_index = 0; edge_index < edge_count; ++edge_index) {
        const MeshEdge& edge = edges[edge_index];
        for (std::uint32_t vertex : {edge.vertex_a, edge.vertex_b}) {
            if (vertex >= vertex_count) {
                throw std::invalid_argument("Cannot build edge exclusions from invalid topology.");
            }
            vertex_edges[vertex].push_back(edge_index);
        }
    }

    const std::size_t words_per_edge = (static_cast<std::size_t>(edge_count) + 31u) / 32u;
    std::vector<std::uint32_t> exclusions(edge_count * words_per_edge, 0u);

    for (const auto& [vertex_a, vertex_b] : edges) {
        for (std::uint32_t first_edge : vertex_edges[vertex_a]) {
            const std::size_t row_offset = first_edge * words_per_edge;
            for (std::uint32_t second_edge : vertex_edges[vertex_b]) {
                exclusions[row_offset + second_edge / 32u] |= 1u << (second_edge % 32u);
                exclusions[second_edge * words_per_edge + first_edge / 32u] |= 1u << (first_edge % 32u);
            }
        }
    }
    return exclusions;
}

std::vector<MeshEdge> build_unique_bending_edges(const std::vector<TriangleVertexIndices>& vertex_indices)
{
    std::vector<std::pair<MeshEdge, std::uint32_t>> edge_opposite_vertices;
    edge_opposite_vertices.reserve(vertex_indices.size() * 3u);
    for (const auto& [vertex_a, vertex_b, vertex_c] : vertex_indices) {

        edge_opposite_vertices.push_back({make_edge(vertex_a, vertex_b), vertex_c});
        edge_opposite_vertices.push_back({make_edge(vertex_b, vertex_c), vertex_a});
        edge_opposite_vertices.push_back({make_edge(vertex_c, vertex_a), vertex_b});
    }

    std::sort(edge_opposite_vertices.begin(), edge_opposite_vertices.end());

    std::vector<MeshEdge> bending_edges;
    for (std::size_t index = 1u; index < edge_opposite_vertices.size(); ++index) {
        const auto& [edge_a, opposite_vertex_a] = edge_opposite_vertices[index - 1u];
        const auto& [edge_b, opposite_vertex_b] = edge_opposite_vertices[index];
        
        if (edge_a == edge_b && opposite_vertex_a != opposite_vertex_b) {
            bending_edges.push_back(make_edge(opposite_vertex_a, opposite_vertex_b));
        }
    }

    std::sort(bending_edges.begin(), bending_edges.end());
    bending_edges.erase(std::unique(bending_edges.begin(), bending_edges.end()), bending_edges.end());
    
    return bending_edges;
}

// garment converter utils

void orient_triangles_outward(const std::vector<glm::vec3>& vertex_positions,
                              const glm::vec3& bounds_center,
                              std::vector<TriangleVertexIndices>& vertex_indices)
{
    const auto triangle_count = static_cast<std::uint32_t>(vertex_indices.size());

    // Build triangle edge map and neighbors
    std::map<MeshEdge, TriangleEdge> triangle_edge_map;
    std::vector<std::vector<TriangleNeighbor>> triangle_neighbors(triangle_count);
    for (std::uint32_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
        const auto& [vertex_a, vertex_b, vertex_c] = vertex_indices[triangle_index];

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
        const glm::vec3 position_a = vertex_positions[vertex_indices[triangle_index].a];
        const glm::vec3 position_b = vertex_positions[vertex_indices[triangle_index].b];
        const glm::vec3 position_c = vertex_positions[vertex_indices[triangle_index].c];
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

        std::swap(vertex_indices[triangle_index].b, vertex_indices[triangle_index].c);
    }
}

std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<glm::vec3>& vertex_positions)
{
    std::vector<float> lengths;
    lengths.reserve(edges.size());
    for (const MeshEdge& edge : edges) {
        const glm::vec3 vertex_a = vertex_positions[edge.vertex_a];
        const glm::vec3 vertex_b = vertex_positions[edge.vertex_b];
        lengths.push_back(glm::length(vertex_b - vertex_a));
    }
    return lengths;
}

GarmentDistanceConstraints build_distance_constraints(const std::vector<MeshEdge>& edges,
                                                      const std::vector<glm::vec3>& vertex_positions)
{
    if (edges.empty()) {
        throw std::runtime_error("Cannot color an empty edge set.");
    }

    const auto vertex_count = static_cast<std::uint32_t>(vertex_positions.size());
    std::vector<std::vector<MeshEdge>> edge_groups;
    std::vector<std::bitset<max_color_count>> assigned_colors(vertex_count);

    for (const MeshEdge& edge : edges) {
        const std::size_t color_index = assign_edge_color(assigned_colors, edge);
        if (color_index == edge_groups.size()) {
            edge_groups.emplace_back();
        }
        edge_groups[color_index].push_back(edge);
    }

    GarmentDistanceConstraints constraints;
    constraints.colorized_edges.reserve(edges.size());
    constraints.color_states.reserve(edge_groups.size());

    for (const std::vector<MeshEdge>& edge_group : edge_groups) {
        constraints.color_states.push_back({static_cast<std::uint32_t>(constraints.colorized_edges.size()),
                                            static_cast<std::uint32_t>(edge_group.size())});

        constraints.colorized_edges.insert(constraints.colorized_edges.end(),
                                           edge_group.begin(),
                                           edge_group.end());
    }

    constraints.rest_lengths = compute_mesh_edge_lengths(constraints.colorized_edges, vertex_positions);
    return constraints;
}
