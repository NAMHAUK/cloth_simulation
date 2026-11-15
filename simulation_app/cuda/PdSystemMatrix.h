#pragma once

#include <cstdint>

struct CsrMatrix;
class PdConstraints;

void build_system_matrix(CsrMatrix& matrix,
                         PdConstraints& constraints,
                         const float* masses,
                         std::uint32_t vertex_count,
                         float inv_dt_squared);
