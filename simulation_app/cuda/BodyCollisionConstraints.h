#pragma once

#include <vector>

#include <vector_types.h>

inline constexpr float body_collision_thickness = 0.005f;

struct BodyCollisionConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

inline void initialize_constraints(BodyCollisionConstraint*, const std::vector<GarmentObject>&, const ClothGpuState&)
{}

void project_constraints(const ConstraintSet<BodyCollisionConstraint>& constraints, const float4* positions);
