#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

struct DistanceConstraint;
struct GarmentObject;
class ClothGpuState;

void initialize_constraints(DistanceConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(DistanceConstraint* constraints, std::uint32_t constraint_count, const float4* positions);
