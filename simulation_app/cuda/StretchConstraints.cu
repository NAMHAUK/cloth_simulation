#include "StretchConstraints.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
__global__ void initialize_stretch_constraints_kernel(PdConstraintMatrix* constraint_matrices_ptr,
                                                      std::uint32_t* vertex_indices_ptr,
                                                      float* A_values_ptr,
                                                      float* B_values_ptr,
                                                      float* rhs_values_ptr,
                                                      float3* p_ptr)
{
    vertex_indices_ptr[0] = 0;
    vertex_indices_ptr[1] = 1;

    A_values_ptr[0] = 1.0f;
    A_values_ptr[1] = -1.0f;

    B_values_ptr[0] = 1.0f;

    PdConstraintMatrix& constraint = constraint_matrices_ptr[0];
    constraint.vertex_indices_ptr = vertex_indices_ptr;
    constraint.vertex_count = 2;
    constraint.A = {A_values_ptr, 1, constraint.vertex_count, constraint.vertex_count};
    constraint.B = {B_values_ptr, 1, 1, 1};
    constraint.rhs_matrix = {rhs_values_ptr, constraint.vertex_count, 1, 1};
    constraint.p_ptr = p_ptr;
    constraint.weight = 1.0f;
}

__global__ void project_stretch_constraints_kernel(float3* p_ptr)
{
    p_ptr[0] = make_float3(1.0f, 2.0f, 3.0f);
}
}

void initialize_stretch_constraints(PdConstraintMatrix* constraint_matrices_ptr,
                                    std::uint32_t* vertex_indices_ptr,
                                    float* A_values_ptr,
                                    float* B_values_ptr,
                                    float* rhs_values_ptr,
                                    float3* p_ptr)
{
    initialize_stretch_constraints_kernel<<<1, 1>>>(constraint_matrices_ptr,
                                                    vertex_indices_ptr,
                                                    A_values_ptr,
                                                    B_values_ptr,
                                                    rhs_values_ptr,
                                                    p_ptr);
    CUDA_CHECK(cudaGetLastError());
}

void project_stretch_constraints(float3* p_ptr)
{
    project_stretch_constraints_kernel<<<1, 1>>>(p_ptr);
    CUDA_CHECK(cudaGetLastError());
}
