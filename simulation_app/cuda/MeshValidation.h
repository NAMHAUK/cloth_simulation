#pragma once

#include <cstdint>

#include <vector_types.h>

struct TriangleVertexIndices;

bool validate_triangles(const float4* positions,
                        const TriangleVertexIndices* triangle_vertex_indices,
                        std::uint32_t triangle_count);
