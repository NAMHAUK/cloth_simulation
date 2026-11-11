#include "gpu/pd/PdGpuState.h"
#include "StretchConstraints.h"
#include "utils/CudaUtils.h"

void PdGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    cloth_gpu_state_.rebuild_buffers(cloth_state);

    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&constraint_matrix_buffer_), sizeof(PdConstraintMatrix)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&constraint_vertex_index_buffer_), 2 * sizeof(std::uint32_t)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&constraint_A_value_buffer_), 2 * sizeof(float)));
}

void PdGpuState::prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    if (cloth_gpu_state_.vertex_count() == 0) {
        return;
    }

    cloth_gpu_state_.initialize_masses(garments, cloth_state);

    // TODO: 각 constraint의 입력 확보·GPU 저장
    CUDA_CHECK(initialize_stretch_constraints(constraint_matrix_buffer_,
                                              constraint_vertex_index_buffer_,
                                              constraint_A_value_buffer_));

    // TODO: H의 저장 공간 준비
    // TODO: H를 관성 항으로 초기화
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
    cloth_gpu_state_.release();

    if (constraint_matrix_buffer_ != nullptr) {
        CUDA_CHECK(cudaFree(constraint_matrix_buffer_));
        constraint_matrix_buffer_ = nullptr;
    }
    if (constraint_vertex_index_buffer_ != nullptr) {
        CUDA_CHECK(cudaFree(constraint_vertex_index_buffer_));
        constraint_vertex_index_buffer_ = nullptr;
    }
    if (constraint_A_value_buffer_ != nullptr) {
        CUDA_CHECK(cudaFree(constraint_A_value_buffer_));
        constraint_A_value_buffer_ = nullptr;
    }
}
