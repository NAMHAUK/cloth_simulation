#include "StretchConstraints.h"
#include "gpu/pd/PdGpuState.h"

#include <cuda_runtime.h>

namespace {
__global__ void initialize_stretch_constraints_kernel(PdConstraintMatrix* constraint_matrices_ptr,
                                                      std::uint32_t* vertex_indices_ptr,
                                                      float* A_values_ptr)
{
    vertex_indices_ptr[0] = 0;
    vertex_indices_ptr[1] = 1;

    A_values_ptr[0] = 1.0f;
    A_values_ptr[1] = -1.0f;

    constraint_matrices_ptr[0] = {{A_values_ptr, 1, 2, 2}, vertex_indices_ptr, 1.0f};
}
}

cudaError_t initialize_stretch_constraints(PdConstraintMatrix* constraint_matrices_ptr,
                                           std::uint32_t* vertex_indices_ptr,
                                           float* A_values_ptr)
{
    initialize_stretch_constraints_kernel<<<1, 1>>>(constraint_matrices_ptr, vertex_indices_ptr, A_values_ptr);
    return cudaGetLastError();
}
