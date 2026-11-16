#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

struct TriangleVertexIndices;

void update_normals(const float4* positions,
                    float4* normals,
                    const TriangleVertexIndices* triangle_vertex_indices,
                    std::uint32_t vertex_count,
                    const std::vector<std::uint32_t>& triangle_color_counts);
