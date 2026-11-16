#pragma once

#include "gpu/pd/PdCharacterGpuState.h"
#include "gpu/pd/PdClothGpuState.h"
#include "gpu/pd/PdConstraints.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;

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
    void prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    PdClothGpuState& cloth_gpu_state();
    const PdClothGpuState& cloth_gpu_state() const;

    PdConstraints& constraints();
    const CsrMatrix& system_matrix() const;
    float3* inertia_rhs_buffer() const;
    float3* rhs_buffer() const;
    void set_rhs(const float3* source);

    void release_simulation_buffers();
    void release_garment_resources();
    void release();

private:
    friend void prepare_pd_system(PdGpuState& state);

    PdCharacterGpuState character_gpu_state_;
    PdClothGpuState cloth_gpu_state_;
    float inv_dt_squared_ = 0.0f;

    PdConstraints constraints_;

    CsrMatrix system_matrix_;
    float3* inertia_rhs_buffer_ = nullptr;
    float3* rhs_buffer_ = nullptr;
};
