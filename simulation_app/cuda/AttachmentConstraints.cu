#include "AttachmentConstraints.h"
#include "gpu/pd/ConstraintTypes.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void initialize_attachment_constraints_kernel(AttachmentConstraint* constraints,
                                                         std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    AttachmentConstraint& constraint = constraints[index];
    constraint = AttachmentConstraint{};
    constraint.vertex_indices[0] = 0;

    constraint.A[0][0] = 1.0f;
    constraint.B[0][0] = 1.0f;

    constraint.weight = 1.0f;
}

__global__ void project_attachment_constraints_kernel(AttachmentConstraint* constraints, std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    constraints[index].p[0] = make_float3(0.0f, 1.0f, 0.0f);
}
}

void initialize_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count)
{
    initialize_attachment_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(
        constraints,
        constraint_count);
    CUDA_CHECK(cudaGetLastError());
}

void project_constraints(AttachmentConstraint* constraints, std::uint32_t constraint_count, const float4*)
{
    project_attachment_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(constraints,
                                                                                                     constraint_count);
    CUDA_CHECK(cudaGetLastError());
}
