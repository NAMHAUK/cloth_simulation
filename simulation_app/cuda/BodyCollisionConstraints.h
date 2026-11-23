#pragma once

#include "simulation/pd/PdSimulationParams.h"

#include <vector>

#include <vector_types.h>

inline constexpr float body_collision_thickness = PdSimulationParams::body_collision_thickness;

struct BodyCollisionConstraint;
struct GarmentObject;
class ClothGpuState;
template <typename Constraint>
struct ConstraintSet;

inline void initialize_constraints(BodyCollisionConstraint*, const std::vector<GarmentObject>&, const ClothGpuState&)
{}

void project_constraints(const ConstraintSet<BodyCollisionConstraint>& constraints, const float4* positions);
