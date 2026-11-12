#include "gpu/pd/PdGpuState.h"
#include "PdSystemMatrix.h"
#include "StretchConstraints.h"
#include "utils/CudaUtils.h"

#include <stdexcept>

#include <cuda_runtime.h>

void PdGpuState::initialize(float substep_dt)
{
    inv_dt_squared_ = 1.0f / (substep_dt * substep_dt);
}

void PdGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    cloth_gpu_state_.rebuild_buffers(cloth_state);

    total_constraint_count_ = 1;
    CUDA_CHECK(cudaMalloc(&constraint_matrix_buffer_, total_constraint_count_ * sizeof(PdConstraintMatrix)));
    CUDA_CHECK(cudaMalloc(&constraint_vertex_index_buffer_, 2 * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(&constraint_A_value_buffer_, 2 * sizeof(float)));
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

    // TODO: 각 constraint의 입력 확보·GPU 저장
    CUDA_CHECK(initialize_stretch_constraints(constraint_matrix_buffer_,
                                              constraint_vertex_index_buffer_,
                                              constraint_A_value_buffer_));

    release_system_matrix();
    build_system_matrix(system_matrix_,
                        constraint_matrix_buffer_,
                        total_constraint_count_,
                        cloth_gpu_state_.masses(),
                        cloth_gpu_state_.vertex_count(),
                        inv_dt_squared_);

    // TODO: 각 constraint의 행렬 기여분 누적
}

PdClothGpuState& PdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PdClothGpuState& PdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

void PdGpuState::release()
{
    release_system_matrix();
    cloth_gpu_state_.release();

    CUDA_CHECK(cudaFree(constraint_matrix_buffer_));
    CUDA_CHECK(cudaFree(constraint_vertex_index_buffer_));
    CUDA_CHECK(cudaFree(constraint_A_value_buffer_));

    constraint_matrix_buffer_ = nullptr;
    constraint_vertex_index_buffer_ = nullptr;
    constraint_A_value_buffer_ = nullptr;
    total_constraint_count_ = 0;
}

void PdGpuState::release_system_matrix()
{
    CUDA_CHECK(cudaFree(system_matrix_.row_offsets_ptr));
    CUDA_CHECK(cudaFree(system_matrix_.column_indices_ptr));
    CUDA_CHECK(cudaFree(system_matrix_.values_ptr));

    system_matrix_.row_offsets_ptr = nullptr;
    system_matrix_.column_indices_ptr = nullptr;
    system_matrix_.values_ptr = nullptr;
}
