#pragma once

#include <vector_types.h>

class PdClothGpuState;

void integrate_cloth(float4* positions, const PdClothGpuState& cloth_state, float gravity, float dt);

void update_cloth_velocities(const float4* positions, const PdClothGpuState& cloth_state, float dt);
