#pragma once

#include <vector_types.h>

class PdClothGpuState;
struct TriangleVertexIndices;

void update_cloth_normals(const float4* positions,
                          float4* normals,
                          const TriangleVertexIndices* triangle_vertex_indices,
                          const PdClothGpuState& cloth_state);
