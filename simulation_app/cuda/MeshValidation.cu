#include "MeshValidation.h"
#include "asset/AssetDataTypes.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr std::uint32_t block_size = 256;
__device__ std::uint32_t failure_flag = 0;

__global__ void validate_triangles_kernel(const float4* positions,
                                          const TriangleVertexIndices* triangle_vertex_indices,
                                          std::uint32_t triangle_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= triangle_count) {
        return;
    }

    const TriangleVertexIndices indices = triangle_vertex_indices[index];
    const glm::vec3 a = glm::make_vec3(&positions[indices.a].x);
    const glm::vec3 b = glm::make_vec3(&positions[indices.b].x);
    const glm::vec3 c = glm::make_vec3(&positions[indices.c].x);
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 normal = glm::cross(ab, ac);
    const float normal_squared = glm::dot(normal, normal);
    const float d00 = glm::dot(ab, ab);
    const float d01 = glm::dot(ab, ac);
    const float d11 = glm::dot(ac, ac);
    const float denominator = d00 * d11 - d01 * d01;
    if (!isfinite(normal_squared) || normal_squared <= 1.0e-20f || !isfinite(denominator) || denominator <= 1.0e-20f) {
        atomicExch(&failure_flag, 1u);
    }
}
}

bool validate_triangles(const float4* positions,
                        const TriangleVertexIndices* triangle_vertex_indices,
                        std::uint32_t triangle_count)
{
    std::uint32_t failure = 0;
    CUDA_CHECK(cudaMemcpyToSymbol(failure_flag, &failure, sizeof(failure)));

    validate_triangles_kernel<<<block_count(triangle_count, block_size), block_size>>>(positions,
                                                                                       triangle_vertex_indices,
                                                                                       triangle_count);
    CUDA_CHECK(cudaGetLastError());

    CUDA_CHECK(cudaMemcpyFromSymbol(&failure, failure_flag, sizeof(failure)));
    return failure == 0;
}
