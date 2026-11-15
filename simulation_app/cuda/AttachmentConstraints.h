#pragma once

#include <cstdint>

#include <vector_types.h>

struct AttachmentConstraint;

void initialize_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count);

void project_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count, const float4* positions);
