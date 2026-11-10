#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

VertexTriangleAdjacency build_vertex_triangle_adjacency(
    std::uint32_t vertex_count,
    const std::vector<TriangleVertexIndices>& vertex_indices);

std::vector<std::uint32_t> build_vertex_neighborhood_masks(
    std::uint32_t vertex_count,
    const std::vector<TriangleVertexIndices>& vertex_indices);

std::vector<MeshEdge> build_unique_triangle_edges(const std::vector<TriangleVertexIndices>& vertex_indices);

std::vector<std::uint32_t> build_edge_exclusions(std::uint32_t vertex_count,
                                                 const std::vector<MeshEdge>& edges);

std::vector<MeshEdge> build_unique_bending_edges(const std::vector<TriangleVertexIndices>& vertex_indices);

void orient_triangles_outward(const std::vector<glm::vec3>& vertex_positions,
                              const glm::vec3& bounds_center,
                              std::vector<TriangleVertexIndices>& vertex_indices);

std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<glm::vec3>& vertex_positions);

GarmentDistanceConstraints build_distance_constraints(const std::vector<MeshEdge>& edges,
                                                      const std::vector<glm::vec3>& vertex_positions);
