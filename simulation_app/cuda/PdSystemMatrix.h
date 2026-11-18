#pragma once

#include <cstdint>

class PdGpuState;

struct CsrMatrix
{
    std::int32_t* row_offsets_ptr = nullptr;
    std::int32_t* col_indices_ptr = nullptr;
    float* values_ptr = nullptr;
};

void prepare_pd_system(PdGpuState& state);
