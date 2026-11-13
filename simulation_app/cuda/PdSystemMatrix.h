#pragma once

#include <cstdint>

struct CsrMatrix;
struct DistanceConstraint;
struct AttachmentConstraint;

void build_system_matrix(CsrMatrix& matrix,
                         const DistanceConstraint* constraints,
                         std::uint32_t constraint_count,
                         const AttachmentConstraint* attachment_constraints,
                         std::uint32_t attachment_constraint_count,
                         const float* masses,
                         std::uint32_t vertex_count,
                         float inv_dt_squared);
