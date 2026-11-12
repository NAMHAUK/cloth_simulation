#include "gpu/pd/PdClothGpuState.h"
#include "ClothMass.h"
#include "gpu/common/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

#include <cuda_gl_interop.h>

void PdClothGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    vertex_count_ = cloth_state.element_counts().vertex;

    const std::size_t velocity_bytes = sizeof(float4) * vertex_count_;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&velocity_buffer_), velocity_bytes));
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, velocity_bytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&mass_buffer_), sizeof(float) * vertex_count_));

    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&position_resource_,
                                            cloth_state.buffer_set().current_position,
                                            cudaGraphicsRegisterFlagsNone));
}

void PdClothGpuState::initialize_masses(const std::vector<GarmentObject>& garments, const ClothGpuState& cloth_state)
{
    for (const GarmentObject& garment : garments) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];

        CUDA_CHECK(set_vertex_masses(mass_buffer_ + garment_state.vertex_start_index,
                                     garment_state.vertex_count,
                                     garment.surface_density));
    }
}

void PdClothGpuState::reset_velocities() const
{
    CUDA_CHECK(cudaMemset(velocity_buffer_, 0, sizeof(float4) * vertex_count_));
}

// Shared Buffer Mapping

float4* PdClothGpuState::map_shared_buffers()
{
    // 공유 buffer CUDA 접근 활성화
    CUDA_CHECK(cudaGraphicsMapResources(1, &position_resource_));

    // 공유 buffer pointer 가져오기
    void* positions = nullptr;
    std::size_t position_bytes = 0;
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&positions, &position_bytes, position_resource_));

    return static_cast<float4*>(positions);
}

void PdClothGpuState::unmap_shared_buffers()
{
    CUDA_CHECK(cudaGraphicsUnmapResources(1, &position_resource_));
}

// Accessors

float4* PdClothGpuState::velocity_buffer() const
{
    return velocity_buffer_;
}

const float* PdClothGpuState::mass_buffer() const
{
    return mass_buffer_;
}

std::uint32_t PdClothGpuState::vertex_count() const
{
    return vertex_count_;
}

void PdClothGpuState::release()
{
    // OpenGL과 공유하는 버퍼 사용 등록 해제
    if (position_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(position_resource_));
        position_resource_ = nullptr;
    }

    // CUDA 메모리 해제
    CUDA_CHECK(cudaFree(velocity_buffer_));
    CUDA_CHECK(cudaFree(mass_buffer_));

    velocity_buffer_ = nullptr;
    mass_buffer_ = nullptr;
    vertex_count_ = 0;
}
