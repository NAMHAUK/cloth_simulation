#include "GroundConstraints.h"
#include "gpu/pd/PdConstraints.h"
#include "simulation/SimulationParams.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;
constexpr float ground_height = SimulationParams::ground_height;

__global__ void initialize_ground_constraints_kernel(GroundConstraint* constraints, std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    GroundConstraint& constraint = constraints[index];
    constraint = GroundConstraint{};
    constraint.vertex_indices[0] = index;
    constraint.A[0][0] = 1.0f;
    constraint.B[0][0] = 1.0f;
}

__global__ void project_ground_constraints_kernel(GroundConstraint* constraints,
                                                  std::uint32_t constraint_count,
                                                  const float4* positions)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    GroundConstraint& constraint = constraints[index];
    const float4 position = positions[constraint.vertex_indices[0]];
    constraint.p[0] = make_float3(position.x, fmaxf(position.y, ground_height), position.z);
}
}

void initialize_constraints(GroundConstraint* constraints,
                            const std::vector<GarmentObject>&,
                            const ClothGpuState& cloth_state)
{
    const std::uint32_t constraint_count = cloth_state.element_counts().vertex;
    initialize_ground_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(constraints,
                                                                                                    constraint_count);
    CUDA_CHECK(cudaGetLastError());
}

void project_constraints(const ConstraintSet<GroundConstraint>& constraints, const float4* positions)
{
    project_ground_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(constraints.buffer,
                                                                                                  constraints.count,
                                                                                                  positions);
    CUDA_CHECK(cudaGetLastError());
}
