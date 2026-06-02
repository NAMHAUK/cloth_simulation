#pragma once

#include <cstdint>
#include <vector>

struct VertexFaceAdjacency final {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> face_indices;
    std::uint32_t face_count = 0;

    bool is_valid(std::uint32_t vertex_count) const;
};

struct MeshEdge final {
    std::uint32_t vertex_a = 0;
    std::uint32_t vertex_b = 0;
};

struct MeshEdgeRange final {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct ColorizedMeshEdges final {
    std::vector<MeshEdge> edges;
    std::vector<MeshEdgeRange> ranges;
};

bool build_vertex_face_adjacency(std::uint32_t vertex_count,
                                 const std::vector<std::uint32_t>& triangle_indices,
                                 VertexFaceAdjacency& adjacency);

std::vector<MeshEdge> build_unique_triangle_edges(std::uint32_t vertex_count,
                                                  const std::vector<std::uint32_t>& triangle_indices);

ColorizedMeshEdges colorize_mesh_edges(std::uint32_t vertex_count,
                                       const std::vector<MeshEdge>& edges);

std::vector<float> compute_mesh_edge_lengths(const std::vector<MeshEdge>& edges,
                                             const std::vector<float>& vertices);
