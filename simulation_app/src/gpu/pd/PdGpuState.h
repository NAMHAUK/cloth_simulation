#pragma once

#include "gpu/pd/PdCharacterGpuState.h"
#include "gpu/pd/PdClothGpuState.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;

struct MatrixView
{
    const float* values_ptr = nullptr;
    std::uint32_t rows = 0;
    std::uint32_t columns = 0;
    std::uint32_t row_stride = 0;
};

struct PdConstraintMatrix
{
    MatrixView A;
    MatrixView B;
    MatrixView rhs_matrix;
    const float3* p_ptr = nullptr;
    const std::uint32_t* vertex_indices_ptr = nullptr;
    float weight = 0.0f;
};

struct CsrMatrix
{
    std::uint32_t* row_offsets_ptr = nullptr;
    std::uint32_t* col_indices_ptr = nullptr;
    float* values_ptr = nullptr;
};

class PdGpuState final
{
public:
    PdGpuState() = default;
    PdGpuState(const PdGpuState&) = delete;
    PdGpuState& operator=(const PdGpuState&) = delete;

    void initialize(float dt);
    void rebuild_buffers(const ClothGpuState& cloth_state);
    void prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    PdClothGpuState& cloth_gpu_state();
    const PdClothGpuState& cloth_gpu_state() const;
    const PdConstraintMatrix* constraint_matrix_buffer() const;
    std::uint32_t constraint_count() const;
    float3* constraint_p_buffer() const;
    const CsrMatrix& system_matrix() const;
    float3* rhs_buffer() const;

    void release();

private:
    void release_system_matrix();

    PdCharacterGpuState character_gpu_state_;
    PdClothGpuState cloth_gpu_state_;
    float inv_dt_squared_ = 0.0f;

    // constraint_matrix_buffer_[i]
    // ┌────────────────────┐
    // │ A.values_ptr       ├──▶ constraint_A_value_buffer_      [ ... | 각 constraint A matrix 계수 | ... ]
    // │ vertex_indices_ptr ├──▶ constraint_vertex_index_buffer_ [ ... | 각 constraint vertex indices | ... ]  
    // │ weight             │
    // └────────────────────┘

    PdConstraintMatrix* constraint_matrix_buffer_ = nullptr;
    std::uint32_t* constraint_vertex_index_buffer_ = nullptr;
    float* constraint_A_value_buffer_ = nullptr;
    float* constraint_B_value_buffer_ = nullptr;
    float* constraint_rhs_value_buffer_ = nullptr;
    float3* constraint_p_buffer_ = nullptr;
    std::uint32_t total_constraint_count_ = 0;

    CsrMatrix system_matrix_;
    float3* rhs_buffer_ = nullptr;
};
