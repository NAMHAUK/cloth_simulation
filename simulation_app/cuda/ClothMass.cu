#include "ClothMass.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>

namespace {
__global__ void set_vertex_masses_kernel(float* masses, std::uint32_t vertex_count, float mass)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < vertex_count) {
        masses[index] = mass;
    }
}
}

cudaError_t set_vertex_masses(float* masses, std::uint32_t vertex_count, float mass)
{
    constexpr std::uint32_t block_size = 256;
    set_vertex_masses_kernel<<<block_count(vertex_count, block_size), block_size>>>(masses, vertex_count, mass);
    return cudaGetLastError();
}
