#include "BendingConstraints.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/ClothGpuState.h"
#include "gpu/pd/ConstraintTypes.h"
#include "gpu/pd/PdConstraints.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thrust/device_vector.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void initialize_bending_constraints_kernel(BendingConstraint* constraints,
                                                      const glm::vec3* rest_positions,
                                                      const MeshEdge* edges,
                                                      std::uint32_t constraint_count,
                                                      std::uint32_t vertex_offset,
                                                      float weight)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    const MeshEdge edge = edges[index];
    BendingConstraint& constraint = constraints[index];
    constraint = BendingConstraint{};
    constraint.vertex_indices[0] = edge.vertex_a + vertex_offset;
    constraint.vertex_indices[1] = edge.vertex_b + vertex_offset;
    constraint.A[0][0] = -1.0f;
    constraint.A[0][1] = 1.0f;
    constraint.B[0][0] = 1.0f;
    constraint.weight = weight;

    const glm::vec3 delta = rest_positions[edge.vertex_b] - rest_positions[edge.vertex_a];
    constraint.rest_length = glm::length(delta);
    constraint.p[0] = make_float3(delta.x, delta.y, delta.z);
}

__global__ void project_bending_constraints_kernel(BendingConstraint* constraints,
                                                   std::uint32_t constraint_count,
                                                   const float4* positions)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    BendingConstraint& constraint = constraints[index];
    const glm::vec3 a = glm::make_vec3(&positions[constraint.vertex_indices[0]].x);
    const glm::vec3 b = glm::make_vec3(&positions[constraint.vertex_indices[1]].x);
    const glm::vec3 delta = b - a;
    const float length = glm::length(delta);
    if (length <= 1.0e-6f) {
        return;
    }

    const glm::vec3 p = constraint.rest_length * (delta / length);
    constraint.p[0] = make_float3(p.x, p.y, p.z);
}
}

void initialize_constraints(BendingConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state)
{
    std::uint32_t constraint_offset = 0;
    for (const auto& garment : garments) {
        const auto constraint_count = garment.mesh.bending_constraints.colorized_edges.size();

        thrust::device_vector<glm::vec3> rest_positions(garment.mesh.vertex_positions);
        thrust::device_vector<MeshEdge> edges(garment.mesh.bending_constraints.colorized_edges);

        initialize_bending_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(
            constraints + constraint_offset,
            rest_positions.data().get(),
            edges.data().get(),
            constraint_count,
            cloth_state.garment_states()[garment.layer].vertex_start_index,
            garment.bending_weight);
        CUDA_CHECK(cudaGetLastError());

        constraint_offset += constraint_count;
    }
}

void project_constraints(const ConstraintSet<BendingConstraint>& constraints, const float4* positions)
{
    project_bending_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(constraints.buffer,
                                                                                                   constraints.count,
                                                                                                   positions);
    CUDA_CHECK(cudaGetLastError());
}
