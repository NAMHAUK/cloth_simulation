#include "gpu/pd/PdGpuState.h"
#include "PdRhs.h"
#include "PdSystemMatrix.h"
#include "StretchConstraints.h"
#include "utils/CudaUtils.h"

#include <stdexcept>

#include <cuda_runtime.h>

void PdGpuState::initialize(float dt)
{
    inv_dt_squared_ = 1.0f / (dt * dt);
}

void PdGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    cloth_gpu_state_.rebuild_buffers(cloth_state);

    CUDA_CHECK(cudaMalloc(&rhs_buffer_, sizeof(float3) * cloth_gpu_state_.vertex_count()));

    total_constraint_count_ = 1;
    CUDA_CHECK(cudaMalloc(&constraint_matrix_buffer_, total_constraint_count_ * sizeof(PdConstraintMatrix)));
    CUDA_CHECK(cudaMalloc(&constraint_vertex_index_buffer_, 2 * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&constraint_A_value_buffer_, 2 * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&constraint_B_value_buffer_, sizeof(float)));
    CUDA_CHECK(cudaMalloc(&constraint_rhs_value_buffer_, 2 * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&constraint_p_buffer_, sizeof(float3)));
}

void PdGpuState::prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    if (cloth_gpu_state_.vertex_count() == 0) {
        return;
    }
    if (total_constraint_count_ == 0) {
        throw std::runtime_error("PD cloth requires at least one constraint.");
    }

    cloth_gpu_state_.initialize_masses(garments, cloth_state);

    // TODO: 각 constraint별로 값 추가
    initialize_stretch_constraints(constraint_matrix_buffer_,
                                   constraint_vertex_index_buffer_,
                                   constraint_A_value_buffer_,
                                   constraint_B_value_buffer_,
                                   constraint_rhs_value_buffer_,
                                   constraint_p_buffer_);

    release_system_matrix();
    build_system_matrix(system_matrix_,
                        constraint_matrix_buffer_,
                        total_constraint_count_,
                        cloth_gpu_state_.mass_buffer(),
                        cloth_gpu_state_.vertex_count(),
                        inv_dt_squared_);

    compute_constraint_rhs_matrices(constraint_matrix_buffer_, total_constraint_count_, constraint_rhs_value_buffer_);
}

PdClothGpuState& PdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PdClothGpuState& PdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

const PdConstraintMatrix* PdGpuState::constraint_matrix_buffer() const
{
    return constraint_matrix_buffer_;
}

std::uint32_t PdGpuState::constraint_count() const
{
    return total_constraint_count_;
}

float3* PdGpuState::constraint_p_buffer() const
{
    return constraint_p_buffer_;
}

const CsrMatrix& PdGpuState::system_matrix() const
{
    return system_matrix_;
}

float3* PdGpuState::rhs_buffer() const
{
    return rhs_buffer_;
}

void PdGpuState::release()
{
    release_system_matrix();
    cloth_gpu_state_.release();

    CUDA_CHECK(cudaFree(rhs_buffer_));
    rhs_buffer_ = nullptr;

    CUDA_CHECK(cudaFree(constraint_matrix_buffer_));
    CUDA_CHECK(cudaFree(constraint_vertex_index_buffer_));
    CUDA_CHECK(cudaFree(constraint_A_value_buffer_));
    CUDA_CHECK(cudaFree(constraint_B_value_buffer_));
    CUDA_CHECK(cudaFree(constraint_rhs_value_buffer_));
    CUDA_CHECK(cudaFree(constraint_p_buffer_));

    constraint_matrix_buffer_ = nullptr;
    constraint_vertex_index_buffer_ = nullptr;
    constraint_A_value_buffer_ = nullptr;
    constraint_B_value_buffer_ = nullptr;
    constraint_rhs_value_buffer_ = nullptr;
    constraint_p_buffer_ = nullptr;
    total_constraint_count_ = 0;
}

void PdGpuState::release_system_matrix()
{
    CUDA_CHECK(cudaFree(system_matrix_.row_offsets_ptr));
    CUDA_CHECK(cudaFree(system_matrix_.col_indices_ptr));
    CUDA_CHECK(cudaFree(system_matrix_.values_ptr));

    system_matrix_.row_offsets_ptr = nullptr;
    system_matrix_.col_indices_ptr = nullptr;
    system_matrix_.values_ptr = nullptr;
}
