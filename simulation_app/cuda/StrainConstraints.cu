#include "StrainConstraints.h"
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

__global__ void project_strain_constraints_kernel(StrainConstraint* constraints,
                                                  std::uint32_t constraint_count,
                                                  const float4* positions)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }
    StrainConstraint& constraint = constraints[index];

    // deformation_gradient: rest 기준 현재 삼각형의 변형 정도 저장
    // F = [a, b, c] Aᵀ
    const glm::vec3 a = glm::make_vec3(&positions[constraint.vertex_indices[0]].x);
    const glm::vec3 b = glm::make_vec3(&positions[constraint.vertex_indices[1]].x);
    const glm::vec3 c = glm::make_vec3(&positions[constraint.vertex_indices[2]].x);

    glm::vec3 deformation_gradient[2] = {
        constraint.A[0][0] * a + constraint.A[0][1] * b + constraint.A[0][2] * c,
        constraint.A[1][0] * a + constraint.A[1][1] * b + constraint.A[1][2] * c,
    };

    // SVD를 통해 F의 회전과 형태 변형을 분리, 형태 변형을 제거하고 회전만 남김
    // 형태 변형이 없는 목표 상태의 위치 p 계산
    // F = UΣVᵀ → P = UVᵀ

    const glm::vec3& f0 = deformation_gradient[0];
    const glm::vec3& f1 = deformation_gradient[1];
    const float angle = 0.5f * atan2f(2.0f * glm::dot(f0, f1), glm::dot(f0, f0) - glm::dot(f1, f1));
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    glm::vec3 u0 = cosine * f0 + sine * f1;
    glm::vec3 u1 = -sine * f0 + cosine * f1;

    const float sigma0 = glm::length(u0);
    u0 = sigma0 > 0.0f ? u0 / sigma0 : glm::vec3(1.0f, 0.0f, 0.0f);
    u1 -= glm::dot(u0, u1) * u0;
    const float sigma1 = glm::length(u1);
    
    if (sigma1 > 1.0e-6f * sigma0) {
        u1 /= sigma1;
    } else {
        const glm::vec3 axis = fabsf(u0.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        u1 = glm::normalize(glm::cross(u0, axis));
    }

    // projection 결과 저장
    const glm::vec3 p0 = cosine * u0 - sine * u1;
    const glm::vec3 p1 = sine * u0 + cosine * u1;
    constraint.p[0] = make_float3(p0.x, p0.y, p0.z);
    constraint.p[1] = make_float3(p1.x, p1.y, p1.z);
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
            garment.strain_weight);
        CUDA_CHECK(cudaGetLastError());
    }
}

void project_constraints(const ConstraintSet<StrainConstraint>& constraints, const float4* positions)
{
    project_strain_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(constraints.buffer,
                                                                                                  constraints.count,
                                                                                                  positions);
    CUDA_CHECK(cudaGetLastError());
}
