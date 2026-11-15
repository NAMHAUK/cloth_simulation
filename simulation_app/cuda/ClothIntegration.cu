#include "ClothIntegration.h"
#include "gpu/pd/PdClothGpuState.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void integrate_cloth_kernel(float4* positions,
                                       float4* previous_positions,
                                       float4* predicted_positions,
                                       const float4* velocities,
                                       std::uint32_t vertex_count,
                                       float gravity,
                                       float dt)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }

    float4 velocity = velocities[index];
    velocity.y += gravity * dt;

    float4 position = positions[index];
    previous_positions[index] = position;

    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    position.z += velocity.z * dt;

    predicted_positions[index] = position;
    positions[index] = position;
}

__global__ void update_cloth_velocities_kernel(const float4* positions,
                                               const float4* previous_positions,
                                               float4* velocities,
                                               std::uint32_t vertex_count,
                                               float inv_dt)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }

    const float4 position = positions[index];
    const float4 previous_position = previous_positions[index];
    float4 velocity = velocities[index];

    // v = (final_pos - prev_pos) / dt
    velocity.x = (position.x - previous_position.x) * inv_dt;
    velocity.y = (position.y - previous_position.y) * inv_dt;
    velocity.z = (position.z - previous_position.z) * inv_dt;

    velocities[index] = velocity;
}
}

void integrate_cloth(float4* positions, const PdClothGpuState& cloth_state, float gravity, float dt)
{
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    integrate_cloth_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        positions,
        cloth_state.previous_position_buffer(),
        cloth_state.predicted_position_buffer(),
        cloth_state.velocity_buffer(),
        vertex_count,
        gravity,
        dt);
    CUDA_CHECK(cudaGetLastError());
}

void update_cloth_velocities(const float4* positions, const PdClothGpuState& cloth_state, float dt)
{
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    update_cloth_velocities_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        positions,
        cloth_state.previous_position_buffer(),
        cloth_state.velocity_buffer(),
        vertex_count,
        1.0f / dt);
    CUDA_CHECK(cudaGetLastError());
}
