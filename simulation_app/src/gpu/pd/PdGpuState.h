#pragma once

#include "PdSystemMatrix.h"
#include "gpu/pd/PdCharacterGpuState.h"
#include "gpu/pd/PdClothGpuState.h"
#include "gpu/pd/PdConstraints.h"
#include "utils/CudssUtils.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class ClothGpuState;
struct GarmentObject;

class PdGpuState final
{
public:
    struct CollisionCandidateBuffers final
    {
        uint2* pairs = nullptr;
        std::uint32_t* count = nullptr;
        std::uint32_t capacity = 0;
    };

    PdGpuState() = default;
    PdGpuState(const PdGpuState&) = delete;
    PdGpuState& operator=(const PdGpuState&) = delete;

    void initialize(float dt);
    void prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state);
    PdCharacterGpuState& character_gpu_state();
    PdClothGpuState& cloth_gpu_state();
    const PdClothGpuState& cloth_gpu_state() const;

    PdConstraints& constraints();
    const CudssSystem& system_matrix() const;
    float* inertia_rhs_buffer() const;
    float* rhs_buffer() const;
    void set_rhs(const float* source);

    const CollisionCandidateBuffers& body_collision_candidates() const;

    void release_simulation_buffers();
    void release_garment_resources();
    void release();

private:
    friend void prepare_pd_system(PdGpuState& state);

    PdCharacterGpuState character_gpu_state_;
    PdClothGpuState cloth_gpu_state_;
    float inv_dt_squared_ = 0.0f;

    PdConstraints constraints_;

    CudssSystem system_matrix_;
    CsrMatrix system_matrix_csr_;
    float* inertia_rhs_buffer_ = nullptr;
    float* rhs_buffer_ = nullptr;

    CollisionCandidateBuffers body_collision_candidates_;
};
