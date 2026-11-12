#pragma once

#include <cstdint>

struct CsrMatrix;
struct PdConstraintMatrix;

void build_system_matrix(CsrMatrix& matrix,
                         const PdConstraintMatrix* constraints,
                         std::uint32_t constraint_count,
                         const float* masses,
                         std::uint32_t vertex_count,
                         float inv_dt_squared);
