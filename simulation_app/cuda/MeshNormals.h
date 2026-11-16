#pragma once

#include <vector_types.h>

class PdClothGpuState;

void update_cloth_normals(const float4* positions, float4* normals, const PdClothGpuState& cloth_state);
