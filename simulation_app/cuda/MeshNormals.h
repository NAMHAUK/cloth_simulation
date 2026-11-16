#pragma once

#include <cstdint>
#include <vector>

struct MappedMeshBuffers;

void update_normals(const MappedMeshBuffers& buffers,
                    std::uint32_t vertex_count,
                    const std::vector<std::uint32_t>& triangle_color_counts);
