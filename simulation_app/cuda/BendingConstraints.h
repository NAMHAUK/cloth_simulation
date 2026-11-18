#pragma once

#include <vector>

#include <vector_types.h>

struct BendingConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

void initialize_constraints(BendingConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(const ConstraintSet<BendingConstraint>& constraints, const float4* positions);
