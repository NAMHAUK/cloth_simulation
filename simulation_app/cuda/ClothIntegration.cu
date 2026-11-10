#include "ClothIntegration.h"

#include <cuda_runtime.h>

namespace {
__global__ void integrate_cloth_kernel(float4* positions,
                                       float4* velocities,
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
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    position.z += velocity.z * dt;

    velocities[index] = velocity;
    positions[index] = position;
}
}

cudaError_t integrate_cloth(float4* positions, float4* velocities, std::uint32_t vertex_count, float gravity, float dt)
{
    if (vertex_count == 0) {
        return cudaSuccess;
    }

    constexpr std::uint32_t block_size = 256;
    const std::uint32_t block_count = (vertex_count - 1) / block_size + 1;
    integrate_cloth_kernel<<<block_count, block_size>>>(positions, velocities, vertex_count, gravity, dt);
    return cudaGetLastError();
}
