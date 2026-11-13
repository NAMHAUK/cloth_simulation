#include "gpu/pd/PdGpuState.h"
#include "AttachmentConstraints.h"
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

    CUDA_CHECK(cudaMalloc(&inertia_rhs_buffer_, sizeof(float3) * cloth_gpu_state_.vertex_count()));
    CUDA_CHECK(cudaMalloc(&rhs_buffer_, sizeof(float3) * cloth_gpu_state_.vertex_count()));

    stretch_constraint_count_ = 1;
    CUDA_CHECK(cudaMalloc(&stretch_constraint_buffer_, stretch_constraint_count_ * sizeof(DistanceConstraint)));

    attachment_constraint_count_ = 1;
    CUDA_CHECK(cudaMalloc(&attachment_constraint_buffer_, attachment_constraint_count_ * sizeof(AttachmentConstraint)));
}

void PdGpuState::prepare_system(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    if (cloth_gpu_state_.vertex_count() == 0) {
        return;
    }
    if (stretch_constraint_count_ == 0) {
        throw std::runtime_error("PD cloth requires at least one constraint.");
    }

    cloth_gpu_state_.initialize_masses(garments, cloth_state);

    // TODO: 각 constraint별로 값 추가
    initialize_stretch_constraints(stretch_constraint_buffer_);
    initialize_attachment_constraints(attachment_constraint_buffer_, attachment_constraint_count_);

    release_system_matrix();
    build_system_matrix(system_matrix_,
                        stretch_constraint_buffer_,
                        stretch_constraint_count_,
                        attachment_constraint_buffer_,
                        attachment_constraint_count_,
                        cloth_gpu_state_.mass_buffer(),
                        cloth_gpu_state_.vertex_count(),
                        inv_dt_squared_);

    compute_constraint_rhs_matrices(stretch_constraint_buffer_, stretch_constraint_count_);
    compute_constraint_rhs_matrices(attachment_constraint_buffer_, attachment_constraint_count_);
}

// Setter
void PdGpuState::set_rhs(const float3* source)
{
    CUDA_CHECK(cudaMemcpyAsync(rhs_buffer_,
                               source,
                               sizeof(float3) * cloth_gpu_state_.vertex_count(),
                               cudaMemcpyDeviceToDevice));
}

PdClothGpuState& PdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PdClothGpuState& PdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

DistanceConstraint* PdGpuState::stretch_constraint_buffer() const
{
    return stretch_constraint_buffer_;
}

std::uint32_t PdGpuState::stretch_constraint_count() const
{
    return stretch_constraint_count_;
}

AttachmentConstraint* PdGpuState::attachment_constraint_buffer() const
{
    return attachment_constraint_buffer_;
}

std::uint32_t PdGpuState::attachment_constraint_count() const
{
    return attachment_constraint_count_;
}

const CsrMatrix& PdGpuState::system_matrix() const
{
    return system_matrix_;
}

float3* PdGpuState::inertia_rhs_buffer() const
{
    return inertia_rhs_buffer_;
}

float3* PdGpuState::rhs_buffer() const
{
    return rhs_buffer_;
}

void PdGpuState::release()
{
    release_system_matrix();
    cloth_gpu_state_.release();

    CUDA_CHECK(cudaFree(inertia_rhs_buffer_));
    inertia_rhs_buffer_ = nullptr;

    CUDA_CHECK(cudaFree(rhs_buffer_));
    rhs_buffer_ = nullptr;

    CUDA_CHECK(cudaFree(stretch_constraint_buffer_));
    stretch_constraint_buffer_ = nullptr;
    stretch_constraint_count_ = 0;

    CUDA_CHECK(cudaFree(attachment_constraint_buffer_));
    attachment_constraint_buffer_ = nullptr;
    attachment_constraint_count_ = 0;
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
