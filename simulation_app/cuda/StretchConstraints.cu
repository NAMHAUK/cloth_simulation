#include "StretchConstraints.h"
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
constexpr float stretch_limit_weight = 10000.0f;
constexpr float max_stretch_ratio = 1.03f;

__global__ void initialize_stretch_constraints_kernel(StretchConstraint* constraints,
                                                      const glm::vec3* rest_positions,
                                                      const MeshEdge* edges,
                                                      const float* rest_lengths,
                                                      std::uint32_t constraint_count,
                                                      std::uint32_t vertex_offset,
                                                      float weight)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    const MeshEdge edge = edges[index];
    StretchConstraint& constraint = constraints[index];
    
    constraint = StretchConstraint{};
    constraint.vertex_indices[0] = edge.vertex_a + vertex_offset;
    constraint.vertex_indices[1] = edge.vertex_b + vertex_offset;
    constraint.A[0][0] = -1.0f;
    constraint.A[0][1] = 1.0f;
    constraint.B[0][0] = 1.0f;
    constraint.weight = weight + stretch_limit_weight;
    constraint.rest_length = rest_lengths[index];
    constraint.limit_fraction = stretch_limit_weight / constraint.weight;

    const glm::vec3 delta = rest_positions[edge.vertex_b] - rest_positions[edge.vertex_a];
    const glm::vec3 p = constraint.rest_length * glm::normalize(delta);
    constraint.p[0] = make_float3(p.x, p.y, p.z);
}

__global__ void project_stretch_constraints_kernel(StretchConstraint* constraints,
                                                   std::uint32_t constraint_count,
                                                   const float4* positions)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    StretchConstraint& constraint = constraints[index];
    const glm::vec3 a = glm::make_vec3(&positions[constraint.vertex_indices[0]].x);
    const glm::vec3 b = glm::make_vec3(&positions[constraint.vertex_indices[1]].x);
    const glm::vec3 delta = b - a;
    const float length = glm::length(delta);

    const float rest_length = constraint.rest_length;
    const float limit_length = fminf(length, max_stretch_ratio * rest_length);
    const float target_length = rest_length + constraint.limit_fraction * (limit_length - rest_length);

    const glm::vec3 direction = length > 0.0f ? delta : glm::make_vec3(&constraint.p[0].x);
    const glm::vec3 p = target_length * glm::normalize(direction);
    constraint.p[0] = make_float3(p.x, p.y, p.z);
}
}

void initialize_constraints(StretchConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state)
{
    std::uint32_t constraint_offset = 0;
    for (const auto& garment : garments) {
        const auto& mesh = garment.mesh;
        const auto& stretch = mesh.stretch_constraints;
        const auto constraint_count = stretch.colorized_edges.size();

        thrust::device_vector<glm::vec3> rest_positions(mesh.vertex_positions);
        thrust::device_vector<MeshEdge> edges(stretch.colorized_edges);
        thrust::device_vector<float> rest_lengths(stretch.rest_lengths);

        initialize_stretch_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(
            constraints + constraint_offset,
            rest_positions.data().get(),
            edges.data().get(),
            rest_lengths.data().get(),
            constraint_count,
            cloth_state.garment_states()[garment.layer].vertex_start_index,
            garment.stretch_weight);
        CUDA_CHECK(cudaGetLastError());

        constraint_offset += constraint_count;
    }
}

void project_constraints(const ConstraintSet<StretchConstraint>& constraints, const float4* positions)
{
    project_stretch_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(constraints.buffer,
                                                                                                   constraints.count,
                                                                                                   positions);
    CUDA_CHECK(cudaGetLastError());
}
