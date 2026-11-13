#pragma once

#include <cstdint>

struct AttachmentConstraint;

void initialize_attachment_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count);

void project_attachment_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count);
