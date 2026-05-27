#pragma once

#include <cstdint>
#include <vector>

struct VertexTriangleAdjacency final {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> triangles;
    std::uint32_t triangle_count = 0;

    bool is_valid(std::uint32_t vertex_count) const;
};

bool build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                     const std::vector<std::uint32_t>& triangle_indices,
                                     VertexTriangleAdjacency& adjacency);
