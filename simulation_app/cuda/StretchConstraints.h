#pragma once

#include <cstdint>

#include <vector_types.h>

struct DistanceConstraint;

void initialize_constraints(DistanceConstraint* constraints, std::uint32_t constraint_count);

void project_constraints(DistanceConstraint* constraints, std::uint32_t constraint_count, const float4* positions);
