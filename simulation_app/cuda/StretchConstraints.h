#pragma once

#include <cstdint>

#include <cuda_runtime_api.h>

struct PdConstraintMatrix;

cudaError_t initialize_stretch_constraints(PdConstraintMatrix* constraint_matrices_ptr,
                                           std::uint32_t* vertex_indices_ptr,
                                           float* A_values_ptr);
