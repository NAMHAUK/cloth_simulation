#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

VertexTriangleAdjacency build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                                        const std::vector<std::uint32_t>& triangle_indices);

std::vector<std::uint32_t> build_vertex_neighborhood_masks(
    std::uint32_t vertex_count,
    const std::vector<std::uint32_t>& triangle_indices);

glm::vec3 get_vertex_position(const std::vector<float>& vertices, std::uint32_t vertex_index);

std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<float>& vertices);

std::vector<MeshEdge> build_unique_triangle_edges(const std::vector<std::uint32_t>& triangle_indices);

std::vector<std::uint32_t> build_edge_exclusions(std::uint32_t vertex_count,
                                                 const std::vector<std::uint32_t>& edge_indices);

std::vector<MeshEdge> build_unique_bending_edges(const std::vector<std::uint32_t>& triangle_indices);

void orient_triangles_outward(const std::vector<float>& vertices,
                              const glm::vec3& bounds_center,
                              std::vector<std::uint32_t>& triangle_indices);

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count, const std::vector<MeshEdge>& edges);
