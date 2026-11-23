#include "gpu/pd/PdCharacterGpuState.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/CharacterGpuState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

#include <cuda_runtime.h>

// Initialization
void PdCharacterGpuState::initialize(const CharacterGpuState& character_state)
{
    vertex_count_ = character_state.vertex_count();

    shared_buffers.register_buffers(character_state.buffer_set().current_position,
                                    character_state.buffer_set().vertex_normal,
                                    character_state.buffer_set().triangle_vertex_indices);
}

// Motion
void PdCharacterGpuState::set_motion(const CharacterMotion& motion)
{
    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;

    const std::size_t position_bytes = sizeof(float4) * motion.vertex_positions.size();
    CUDA_CHECK(cudaMalloc(&animation_position_buffer_, position_bytes));
    CUDA_CHECK(
        cudaMemcpy(animation_position_buffer_, motion.vertex_positions.data(), position_bytes, cudaMemcpyHostToDevice));
}

void PdCharacterGpuState::update_positions(float4* positions, std::uint32_t frame_index) const
{
    CUDA_CHECK(cudaMemcpyAsync(positions,
                               animation_position_buffer_ + std::size_t{frame_index} * vertex_count_,
                               sizeof(float4) * vertex_count_,
                               cudaMemcpyDeviceToDevice));
}

// Accessors
std::uint32_t PdCharacterGpuState::vertex_count() const
{
    return vertex_count_;
}

// Release
void PdCharacterGpuState::release()
{
    shared_buffers.release();

    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;
    vertex_count_ = 0;
}
