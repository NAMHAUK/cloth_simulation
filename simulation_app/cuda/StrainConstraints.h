#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

struct StrainConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

void initialize_constraints(StrainConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(const ConstraintSet<StrainConstraint>& constraints, const float4* positions);
