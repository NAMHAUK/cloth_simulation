#pragma once

#include <vector>

#include <vector_types.h>

struct GroundConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

void initialize_constraints(GroundConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(const ConstraintSet<GroundConstraint>& constraints,
                         const float4* positions,
                         float ground_height);
