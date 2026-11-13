#pragma once

#include <cstdint>

#include <vector_types.h>

class PdClothGpuState;
struct DistanceConstraint;

void compute_constraint_rhs_matrices(DistanceConstraint* constraints, std::uint32_t constraint_count);

void compute_inertia_rhs(float3* rhs, const PdClothGpuState& cloth_state, float inv_dt_squared);

void accumulate_constraint_rhs(const DistanceConstraint* constraints, std::uint32_t constraint_count, float3* rhs);
