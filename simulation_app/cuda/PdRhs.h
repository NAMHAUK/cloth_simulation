#pragma once

#include <cstdint>

#include <vector_types.h>

class PdClothGpuState;
class PdConstraints;

void compute_constraint_rhs_matrices(PdConstraints& constraints);

void compute_inertia_rhs(float* rhs,
                         const float4* predicted_positions,
                         const PdClothGpuState& cloth_state,
                         float inv_dt_squared);

void accumulate_constraint_rhs(PdConstraints& constraints, float* rhs, std::uint32_t vertex_count);
