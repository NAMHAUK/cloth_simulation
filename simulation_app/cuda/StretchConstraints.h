#pragma once

#include <cstdint>

#include <vector_types.h>

struct PdConstraintMatrix;

void initialize_stretch_constraints(PdConstraintMatrix* constraint_matrices_ptr,
                                    std::uint32_t* vertex_indices_ptr,
                                    float* A_values_ptr,
                                    float* B_values_ptr,
                                    float* rhs_values_ptr,
                                    float3* p_ptr);

void project_stretch_constraints(float3* p_ptr);
