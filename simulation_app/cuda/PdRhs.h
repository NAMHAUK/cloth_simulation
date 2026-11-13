#pragma once

#include <cstdint>

#include <vector_types.h>

class PdClothGpuState;
struct DistanceConstraint;

template <typename Constraint>
void compute_constraint_rhs_matrices(Constraint* constraints, std::uint32_t constraint_count);

void compute_inertia_rhs(float3* rhs, const PdClothGpuState& cloth_state, float inv_dt_squared);

template <typename Constraint>
void accumulate_constraint_rhs(const Constraint* constraints, std::uint32_t constraint_count, float3* rhs);
