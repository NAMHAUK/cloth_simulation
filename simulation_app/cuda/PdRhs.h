#pragma once

#include <cstdint>

#include <vector_types.h>

struct PdConstraintMatrix;

void compute_constraint_rhs_matrices(const PdConstraintMatrix* constraints,
                                     std::uint32_t constraint_count,
                                     float* rhs_values_ptr);

void compute_inertia_rhs(float3* rhs,
                         const float* masses,
                         const float4* predicted_positions,
                         std::uint32_t vertex_count,
                         float inv_dt_squared);

void accumulate_constraint_rhs(const PdConstraintMatrix* constraints, std::uint32_t constraint_count, float3* rhs);
