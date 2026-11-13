#include "PdRhs.h"
#include "gpu/pd/ConstraintTypes.h"
#include "gpu/pd/PdClothGpuState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

template <typename Constraint>
__global__ void compute_constraint_rhs_matrices_kernel(Constraint* constraints, std::uint32_t constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    Constraint& constraint = constraints[index];

    // rhs_matrix = w * A^T * B
    // rhs_matrix[row, column] = weight * sum_k A[k, row] * B[k, column]
    for (std::uint32_t row = 0; row < constraint.vertex_count; ++row) {
        for (std::uint32_t column = 0; column < constraint.projection_vector_count; ++column) {
            float value = 0.0f;

            for (std::uint32_t k = 0; k < constraint.matrix_row_count; ++k) {
                value += constraint.A[k][row] * constraint.B[k][column];
            }

            constraint.rhs_matrix[row][column] = constraint.weight * value;
        }
    }
}

__global__ void compute_inertia_rhs_kernel(float3* rhs,
                                           const float* masses,
                                           const float4* predicted_positions,
                                           std::uint32_t vertex_count,
                                           float inv_dt_squared)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }
    
    // rhs_i = (m_i / h^2) * s_i
    const float coefficient = masses[index] * inv_dt_squared;
    const float4 position = predicted_positions[index];
    rhs[index] = make_float3(coefficient * position.x, coefficient * position.y, coefficient * position.z);
}

template <typename Constraint>
__global__ void accumulate_constraint_rhs_kernel(const Constraint* constraints,
                                                 std::uint32_t constraint_count,
                                                 float3* rhs)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    const Constraint& constraint = constraints[index];

    // rhs_matrix = w · A^T · B
    // rhs += sum (w · S^T · A^T · B · p)
    for (std::uint32_t row = 0; row < constraint.vertex_count; ++row) {
        float3 rhs_value = make_float3(0.0f, 0.0f, 0.0f);

        for (std::uint32_t column = 0; column < constraint.projection_vector_count; ++column) {
            const float coefficient = constraint.rhs_matrix[row][column];
            const float3 p = constraint.p[column];

            rhs_value.x += coefficient * p.x;
            rhs_value.y += coefficient * p.y;
            rhs_value.z += coefficient * p.z;
        }

        float3& vertex_rhs = rhs[constraint.vertex_indices[row]];
        atomicAdd(&vertex_rhs.x, rhs_value.x);
        atomicAdd(&vertex_rhs.y, rhs_value.y);
        atomicAdd(&vertex_rhs.z, rhs_value.z);
    }
}
}

template <typename Constraint>
void compute_constraint_rhs_matrices(Constraint* constraints, std::uint32_t constraint_count)
{
    if (constraint_count == 0) {
        return;
    }

    compute_constraint_rhs_matrices_kernel<<<block_count(constraint_count, block_size), block_size>>>(constraints,
                                                                                                      constraint_count);
    CUDA_CHECK(cudaGetLastError());
}

template void compute_constraint_rhs_matrices<DistanceConstraint>(DistanceConstraint*, std::uint32_t);
template void compute_constraint_rhs_matrices<AttachmentConstraint>(AttachmentConstraint*, std::uint32_t);

void compute_inertia_rhs(float3* rhs, const PdClothGpuState& cloth_state, float inv_dt_squared)
{
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    compute_inertia_rhs_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        rhs,
        cloth_state.mass_buffer(),
        cloth_state.predicted_position_buffer(),
        vertex_count,
        inv_dt_squared);
    CUDA_CHECK(cudaGetLastError());
}

template <typename Constraint>
void accumulate_constraint_rhs(const Constraint* constraints, std::uint32_t constraint_count, float3* rhs)
{
    if (constraint_count == 0) {
        return;
    }

    accumulate_constraint_rhs_kernel<<<block_count(constraint_count, block_size), block_size>>>(constraints,
                                                                                                constraint_count,
                                                                                                rhs);
    CUDA_CHECK(cudaGetLastError());
}

template void accumulate_constraint_rhs<DistanceConstraint>(const DistanceConstraint*, std::uint32_t, float3*);
template void accumulate_constraint_rhs<AttachmentConstraint>(const AttachmentConstraint*, std::uint32_t, float3*);
