#include "gpu/pd/PdGpuState.h"
#include "PdSystemMatrix.h"
#include "gpu/common/ClothGpuState.h"
#include "utils/CudaUtils.h"
#include "utils/CudssUtils.h"

#include <cuda_runtime.h>

// Initialization
void PdGpuState::initialize(float dt)
{
    inv_dt_squared_ = 1.0f / (dt * dt);
}

void PdGpuState::prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    if (cloth_state.element_counts().vertex == 0) {
        return;
    }

    cloth_gpu_state_.create_simulation_buffers(garments, cloth_state);

    constraints_.get<ConstraintIndex::Stretch>().count = 1;
    constraints_.get<ConstraintIndex::Attachment>().count = 1;
    constraints_.allocate_buffers();
    constraints_.initialize();

    CUDA_CHECK(cudaMalloc(&inertia_rhs_buffer_, sizeof(float) * 3 * cloth_gpu_state_.vertex_count()));
    CUDA_CHECK(cudaMalloc(&rhs_buffer_, sizeof(float) * 3 * cloth_gpu_state_.vertex_count()));

    prepare_pd_system(*this);
}

// Setter
void PdGpuState::set_rhs(const float* source)
{
    CUDA_CHECK(cudaMemcpyAsync(rhs_buffer_,
                               source,
                               sizeof(float) * 3 * cloth_gpu_state_.vertex_count(),
                               cudaMemcpyDeviceToDevice));
}

// Accessor
PdCharacterGpuState& PdGpuState::character_gpu_state()
{
    return character_gpu_state_;
}

PdClothGpuState& PdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PdClothGpuState& PdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

PdConstraints& PdGpuState::constraints()
{
    return constraints_;
}

const CudssSystem& PdGpuState::system_matrix() const
{
    return system_matrix_;
}

float* PdGpuState::inertia_rhs_buffer() const
{
    return inertia_rhs_buffer_;
}

float* PdGpuState::rhs_buffer() const
{
    return rhs_buffer_;
}

// Release
void PdGpuState::release_simulation_buffers()
{
    cloth_gpu_state_.release_simulation_buffers();
    constraints_.release_buffers();

    system_matrix_.release();

    CUDA_CHECK(cudaFree(inertia_rhs_buffer_));
    inertia_rhs_buffer_ = nullptr;

    CUDA_CHECK(cudaFree(rhs_buffer_));
    rhs_buffer_ = nullptr;
}

void PdGpuState::release_garment_resources()
{
    release_simulation_buffers();
    cloth_gpu_state_.release_normal_buffers();
}

void PdGpuState::release()
{
    release_garment_resources();
    character_gpu_state_.release();
}
