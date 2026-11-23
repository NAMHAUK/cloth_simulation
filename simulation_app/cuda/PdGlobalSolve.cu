#include "PdGlobalSolve.h"
#include "utils/CudaUtils.h"
#include "utils/CudssUtils.h"

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void store_positions_kernel(const float* solution, float4* positions, std::uint32_t vertex_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }

    positions[index].x = solution[index];
    positions[index].y = solution[vertex_count + index];
    positions[index].z = solution[2 * vertex_count + index];
}
}

void solve_global(const CudssSystem& matrix, float* rhs, float4* positions, std::uint32_t vertex_count)
{
    CUDSS_CHECK(cudssExecute(matrix.handle,
                             CUDSS_PHASE_SOLVE,
                             matrix.config,
                             matrix.decomposed_matrix,
                             matrix.matrix_descriptor,
                             matrix.vector_descriptor,
                             matrix.vector_descriptor));

    store_positions_kernel<<<block_count(vertex_count, block_size), block_size>>>(rhs, positions, vertex_count);
    CUDA_CHECK(cudaGetLastError());
}
