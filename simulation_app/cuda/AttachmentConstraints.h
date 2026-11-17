#pragma once

#include <cstdint>
#include <vector>

#include <vector_types.h>

struct AttachmentConstraint;
struct GarmentObject;
class ClothGpuState;

void initialize_constraints(AttachmentConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state);

void project_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count, const float4* positions);
