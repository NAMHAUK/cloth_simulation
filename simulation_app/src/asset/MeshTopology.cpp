#include "asset/MeshTopology.h"

#include <cstddef>

bool VertexTriangleAdjacency::is_valid(std::uint32_t vertex_count) const
{
    return triangle_count > 0 &&
           offsets.size() == static_cast<std::size_t>(vertex_count) + 1u &&
           !triangles.empty();
}

bool build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                     const std::vector<std::uint32_t>& triangle_indices,
                                     VertexTriangleAdjacency& adjacency)
{
    adjacency = {};

    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return false;
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);
    adjacency.offsets.resize(static_cast<std::size_t>(vertex_count) + 1u, 0);

    for (std::uint32_t index : triangle_indices) {
        if (index >= vertex_count) {
            return false;
        }
        ++adjacency.offsets[static_cast<std::size_t>(index) + 1u];
    }

    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const std::size_t offset_index = static_cast<std::size_t>(vertex_index);
        const std::size_t next_offset_index = offset_index + 1u;
        adjacency.offsets[next_offset_index] += adjacency.offsets[offset_index];
    }

    adjacency.triangles.resize(adjacency.offsets.back(), 0);
    std::vector<std::uint32_t> write_offsets = adjacency.offsets;

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        for (std::uint32_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertex_index = triangle_indices[index_base + corner];
            const std::uint32_t write_index = write_offsets[vertex_index]++;
            adjacency.triangles[write_index] = triangle_index;
        }
    }

    adjacency.triangle_count = triangle_count;
    return adjacency.is_valid(vertex_count);
}
