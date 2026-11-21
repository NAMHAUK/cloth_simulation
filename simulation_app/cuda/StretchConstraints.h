#pragma once

#include <vector>

#include <vector_types.h>

struct StretchConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

void initialize_constraints(StretchConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(const ConstraintSet<StretchConstraint>& constraints, const float4* positions);
