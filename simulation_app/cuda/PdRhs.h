#pragma once

#include <cstdint>

#include <vector_types.h>

class PdClothGpuState;
struct PdConstraintMatrix;

void compute_constraint_rhs_matrices(const PdConstraintMatrix* constraints,
                                     std::uint32_t constraint_count,
                                     float* rhs_values_ptr);

void compute_inertia_rhs(float3* rhs, const PdClothGpuState& cloth_state, float inv_dt_squared);

void accumulate_constraint_rhs(const PdConstraintMatrix* constraints, std::uint32_t constraint_count, float3* rhs);
