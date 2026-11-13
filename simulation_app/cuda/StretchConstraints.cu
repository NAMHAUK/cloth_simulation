#include "StretchConstraints.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
__global__ void initialize_stretch_constraints_kernel(DistanceConstraint* constraints)
{
    DistanceConstraint& constraint = constraints[0];
    constraint.vertex_indices[0] = 0;
    constraint.vertex_indices[1] = 1;

    constraint.A[0][0] = 1.0f;
    constraint.A[0][1] = -1.0f;

    constraint.B[0][0] = 1.0f;

    constraint.weight = 1.0f;
}

__global__ void project_stretch_constraints_kernel(DistanceConstraint* constraints)
{
    constraints[0].p[0] = make_float3(1.0f, 2.0f, 3.0f);
}
}

void initialize_stretch_constraints(DistanceConstraint* constraints)
{
    initialize_stretch_constraints_kernel<<<1, 1>>>(constraints);
    CUDA_CHECK(cudaGetLastError());
}

void project_stretch_constraints(DistanceConstraint* constraints)
{
    project_stretch_constraints_kernel<<<1, 1>>>(constraints);
    CUDA_CHECK(cudaGetLastError());
}
