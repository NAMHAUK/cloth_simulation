#include "gpu/pd/PdClothGpuState.h"
#include "gpu/common/ClothGpuState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

void PdClothGpuState::rebuild_buffers(const ClothGpuState& cloth_state)
{
    vertex_count_ = cloth_state.element_counts().vertex;

    const std::size_t velocity_bytes = sizeof(float4) * vertex_count_;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&velocities_), velocity_bytes));
    CUDA_CHECK(cudaMemset(velocities_, 0, velocity_bytes));

    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&positions_,
                                            cloth_state.buffer_set().current_position,
                                            cudaGraphicsRegisterFlagsNone));
}

void PdClothGpuState::reset_velocities() const
{
    CUDA_CHECK(cudaMemset(velocities_, 0, sizeof(float4) * vertex_count_));
}

// Shared Buffer Mapping

float4* PdClothGpuState::map_shared_buffers() const
{
    // 공유 buffer CUDA 접근 활성화
    cudaGraphicsResource* resource = positions_;
    CUDA_CHECK(cudaGraphicsMapResources(1, &resource));

    // 공유 buffer pointer 가져오기
    void* positions = nullptr;
    std::size_t position_bytes = 0;
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&positions, &position_bytes, resource));

    return static_cast<float4*>(positions);
}

void PdClothGpuState::unmap_shared_buffers() const
{
    cudaGraphicsResource* resource = positions_;
    CUDA_CHECK(cudaGraphicsUnmapResources(1, &resource));
}

// Accessors

float4* PdClothGpuState::velocities() const
{
    return velocities_;
}

std::uint32_t PdClothGpuState::vertex_count() const
{
    return vertex_count_;
}

void PdClothGpuState::release()
{
    // OpenGL과 공유하는 버퍼 사용 등록 해제
    if (positions_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(positions_));
        positions_ = nullptr;
    }

    // CUDA 메모리 해제
    if (velocities_ != nullptr) {
        CUDA_CHECK(cudaFree(velocities_));
        velocities_ = nullptr;
    }
    vertex_count_ = 0;
}
