#pragma once

#include "gpu/pd/PdCharacterGpuState.h"
#include "gpu/pd/PdClothGpuState.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;

struct DistanceConstraint
{
    static constexpr std::uint32_t vertex_count = 2;
    static constexpr std::uint32_t matrix_row_count = 1;
    static constexpr std::uint32_t projection_count = 1;

    float A[matrix_row_count][vertex_count]{};
    float B[matrix_row_count][projection_count]{};
    float rhs_matrix[vertex_count][projection_count]{};
    float3 p[projection_count]{};
    std::uint32_t vertex_indices[vertex_count]{};
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
    DistanceConstraint* stretch_constraint_buffer() const;
    std::uint32_t stretch_constraint_count() const;
    const CsrMatrix& system_matrix() const;
    float3* inertia_rhs_buffer() const;
    float3* rhs_buffer() const;
    void set_rhs(const float3* source);

    void release();

private:
    void release_system_matrix();

    PdCharacterGpuState character_gpu_state_;
    PdClothGpuState cloth_gpu_state_;
    float inv_dt_squared_ = 0.0f;

    DistanceConstraint* stretch_constraint_buffer_ = nullptr;
    std::uint32_t stretch_constraint_count_ = 0;

    CsrMatrix system_matrix_;
    float3* inertia_rhs_buffer_ = nullptr;
    float3* rhs_buffer_ = nullptr;
};
