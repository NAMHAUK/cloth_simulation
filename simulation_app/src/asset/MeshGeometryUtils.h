#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

VertexTriangleAdjacency build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                                        const std::vector<std::uint32_t>& triangle_indices);

bool orient_triangle_winding_outward(std::uint32_t vertex_count,
                                     const std::vector<float>& vertices,
                                     const glm::vec3& reference_point,
                                     std::vector<std::uint32_t>& triangle_indices,
                                     std::uint32_t& flipped_triangle_count);

glm::vec3 get_vertex_position(const std::vector<float>& vertices, std::uint32_t vertex_index);

std::vector<MeshEdge> build_unique_triangle_edges(std::uint32_t vertex_count,
                                                  const std::vector<std::uint32_t>& triangle_indices);

std::vector<MeshEdge> build_unique_bending_edges(std::uint32_t vertex_count,
                                                 const std::vector<std::uint32_t>& triangle_indices);

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count, const std::vector<MeshEdge>& edges);

std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<float>& vertices);
