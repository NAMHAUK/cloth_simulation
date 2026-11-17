#include "StrainConstraints.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/ClothGpuState.h"
#include "gpu/pd/ConstraintTypes.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>
#include <glm/geometric.hpp>
#include <thrust/device_vector.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void initialize_strain_constraints_kernel(StrainConstraint* constraints,
                                                     const glm::vec3* rest_positions,
                                                     const TriangleVertexIndices* triangle_vertex_indices,
                                                     std::uint32_t constraint_count,
                                                     std::uint32_t vertex_offset,
                                                     float weight)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    const TriangleVertexIndices vertex_indices = triangle_vertex_indices[index];
    const glm::vec3& a = rest_positions[vertex_indices.a];
    const glm::vec3& b = rest_positions[vertex_indices.b];
    const glm::vec3& c = rest_positions[vertex_indices.c];
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const float rest_ab_length = glm::length(ab);
    const float rest_area = 0.5f * glm::length(glm::cross(ab, ac));

    float inverse_rest_matrix[2][2];
    inverse_rest_matrix[0][0] = 1.0f / rest_ab_length;
    inverse_rest_matrix[0][1] = -glm::dot(ab, ac) / (2.0f * rest_area * rest_ab_length);
    inverse_rest_matrix[1][0] = 0.0f;
    inverse_rest_matrix[1][1] = rest_ab_length / (2.0f * rest_area);

    StrainConstraint& constraint = constraints[index];
    constraint = StrainConstraint{};
    constraint.vertex_indices[0] = vertex_indices.a + vertex_offset;
    constraint.vertex_indices[1] = vertex_indices.b + vertex_offset;
    constraint.vertex_indices[2] = vertex_indices.c + vertex_offset;
    constraint.weight = weight * rest_area;

    for (std::uint32_t row = 0; row < StrainConstraint::matrix_row_count; ++row) {
        constraint.A[row][0] = -inverse_rest_matrix[0][row] - inverse_rest_matrix[1][row];
        constraint.A[row][1] = inverse_rest_matrix[0][row];
        constraint.A[row][2] = inverse_rest_matrix[1][row];

        constraint.B[row][row] = 1.0f;
    }
}

__global__ void project_strain_constraints_kernel(StrainConstraint* constraints, std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    constraints[index].p[0] = make_float3(1.0f, 0.0f, 0.0f);
    constraints[index].p[1] = make_float3(0.0f, 1.0f, 0.0f);
}
}

void initialize_constraints(StrainConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state)
{
    for (const auto& garment : garments) {
        const auto& garment_state = cloth_state.garment_states()[garment.layer];
        const auto constraint_count = garment_state.triangle_count;

        thrust::device_vector<glm::vec3> rest_positions(
            garment.mesh.vertex_positions.data(),
            garment.mesh.vertex_positions.data() + garment_state.vertex_count);
        thrust::device_vector<TriangleVertexIndices> triangle_vertex_indices(
            garment.mesh.triangle_vertex_indices.data(),
            garment.mesh.triangle_vertex_indices.data() + constraint_count);

        initialize_strain_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(
            constraints + garment_state.triangle_start_index,
            rest_positions.data().get(),
            triangle_vertex_indices.data().get(),
            constraint_count,
            garment_state.vertex_start_index,
            garment.stretch_weight);
        CUDA_CHECK(cudaGetLastError());
    }
}

void project_constraints(StrainConstraint* constraints, std::uint32_t constraint_count, const float4*)
{
    project_strain_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(constraints,
                                                                                                 constraint_count);
    CUDA_CHECK(cudaGetLastError());
}
