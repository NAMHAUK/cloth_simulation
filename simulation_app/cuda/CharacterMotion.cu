#include "CharacterMotion.h"
#include "gpu/pd/PdCharacterGpuState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void interpolate_character_positions_kernel(float4* positions,
                                                       const float4* current_frame,
                                                       const float4* next_frame,
                                                       std::uint32_t vertex_count,
                                                       float frame_alpha)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }

    const float4 current = current_frame[index];
    const float4 next = next_frame[index];
    positions[index] = make_float4((1.0f - frame_alpha) * current.x + frame_alpha * next.x,
                                   (1.0f - frame_alpha) * current.y + frame_alpha * next.y,
                                   (1.0f - frame_alpha) * current.z + frame_alpha * next.z,
                                   (1.0f - frame_alpha) * current.w + frame_alpha * next.w);
}
}

void interpolate_character_positions(float4* positions,
                                     const PdCharacterGpuState& character_state,
                                     std::uint32_t frame_index,
                                     std::uint32_t next_frame_index,
                                     float frame_alpha)
{
    const float4* animation_positions = character_state.animation_position_buffer();
    const std::uint32_t vertex_count = character_state.vertex_count();

    interpolate_character_positions_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        positions,
        animation_positions + std::size_t{frame_index} * vertex_count,
        animation_positions + std::size_t{next_frame_index} * vertex_count,
        vertex_count,
        frame_alpha);
    CUDA_CHECK(cudaGetLastError());
}
