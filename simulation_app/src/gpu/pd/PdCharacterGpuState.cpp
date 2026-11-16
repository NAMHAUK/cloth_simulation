#include "gpu/pd/PdCharacterGpuState.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/CharacterGpuState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

// Initialization
void PdCharacterGpuState::initialize(const CharacterGpuState& character_state)
{
    vertex_count_ = character_state.vertex_count();

    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&position_resource_,
                                            character_state.buffer_set().current_position,
                                            cudaGraphicsRegisterFlagsNone));
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&normal_resource_,
                                            character_state.buffer_set().vertex_normal,
                                            cudaGraphicsRegisterFlagsNone));
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&triangle_resource_,
                                            character_state.buffer_set().triangle_vertex_indices,
                                            cudaGraphicsRegisterFlagsReadOnly));
}

// Motion
void PdCharacterGpuState::set_motion(const CharacterMotion& motion)
{
    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;

    const std::size_t position_bytes = sizeof(float4) * motion.vertex_positions.size();
    CUDA_CHECK(cudaMalloc(&animation_position_buffer_, position_bytes));
    CUDA_CHECK(cudaMemcpy(animation_position_buffer_,
                          motion.vertex_positions.data(),
                          position_bytes,
                          cudaMemcpyHostToDevice));
    frame_count_ = motion.frame_count;
}

void PdCharacterGpuState::update_positions(float4* positions, std::uint32_t frame_index) const
{
    CUDA_CHECK(cudaMemcpyAsync(positions,
                               animation_position_buffer_ + std::size_t{frame_index} * vertex_count_,
                               sizeof(float4) * vertex_count_,
                               cudaMemcpyDeviceToDevice));
}

// Shared Buffer Mapping
void PdCharacterGpuState::map_shared_buffers(float4*& positions, float4*& normals)
{
    cudaGraphicsResource* resources[]{position_resource_, normal_resource_, triangle_resource_};
    CUDA_CHECK(cudaGraphicsMapResources(3, resources));

    void* position_buffer = nullptr;
    void* normal_buffer = nullptr;
    void* triangle_buffer = nullptr;
    std::size_t buffer_bytes = 0;
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&position_buffer, &buffer_bytes, position_resource_));
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&normal_buffer, &buffer_bytes, normal_resource_));
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(&triangle_buffer, &buffer_bytes, triangle_resource_));

    positions = static_cast<float4*>(position_buffer);
    normals = static_cast<float4*>(normal_buffer);
    triangle_vertex_indices_buffer_ = static_cast<const TriangleVertexIndices*>(triangle_buffer);
}

void PdCharacterGpuState::unmap_shared_buffers()
{
    cudaGraphicsResource* resources[]{position_resource_, normal_resource_, triangle_resource_};
    CUDA_CHECK(cudaGraphicsUnmapResources(3, resources));
    triangle_vertex_indices_buffer_ = nullptr;
}

// Accessors
const TriangleVertexIndices* PdCharacterGpuState::triangle_vertex_indices_buffer() const
{
    return triangle_vertex_indices_buffer_;
}

std::uint32_t PdCharacterGpuState::vertex_count() const
{
    return vertex_count_;
}

std::uint32_t PdCharacterGpuState::frame_count() const
{
    return frame_count_;
}

// Release
void PdCharacterGpuState::release()
{
    if (position_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(position_resource_));
        position_resource_ = nullptr;
    }
    if (normal_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(normal_resource_));
        normal_resource_ = nullptr;
    }
    if (triangle_resource_ != nullptr) {
        CUDA_CHECK(cudaGraphicsUnregisterResource(triangle_resource_));
        triangle_resource_ = nullptr;
    }

    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;
    triangle_vertex_indices_buffer_ = nullptr;
    vertex_count_ = 0;
    frame_count_ = 0;
}
