#include "MeshNormals.h"
#include "asset/AssetDataTypes.h"
#include "utils/CudaUtils.h"

#include <cstdint>

#include <cuda_runtime.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void accumulate_vertex_normals_kernel(const float4* positions,
                                                 const TriangleVertexIndices* triangle_vertex_indices,
                                                 std::uint32_t triangle_count,
                                                 float4* normals)
{
    const std::uint32_t triangle_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (triangle_index >= triangle_count) {
        return;
    }

    const auto vertex_indices = triangle_vertex_indices[triangle_index];
    const float4 a = positions[vertex_indices.a];
    const float4 b = positions[vertex_indices.b];
    const float4 c = positions[vertex_indices.c];
    const float3 ab = make_float3(b.x - a.x, b.y - a.y, b.z - a.z);
    const float3 ac = make_float3(c.x - a.x, c.y - a.y, c.z - a.z);
    const float3 normal = make_float3(ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x);

    for (std::uint32_t vertex_index : {vertex_indices.a, vertex_indices.b, vertex_indices.c}) {
        normals[vertex_index].x += normal.x;
        normals[vertex_index].y += normal.y;
        normals[vertex_index].z += normal.z;
    }
}

__global__ void normalize_vertex_normals_kernel(std::uint32_t vertex_count, float4* normals)
{
    const std::uint32_t vertex_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (vertex_index >= vertex_count) {
        return;
    }

    const float4 normal = normals[vertex_index];
    const float length_squared = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (length_squared > 1.0e-20f) {
        const float inverse_length = rsqrtf(length_squared);
        normals[vertex_index] = make_float4(normal.x * inverse_length,
                                            normal.y * inverse_length,
                                            normal.z * inverse_length,
                                            0.0f);
    } else {
        normals[vertex_index] = make_float4(0.0f, 1.0f, 0.0f, 0.0f);
    }
}
}

void update_normals(const float4* positions,
                    float4* normals,
                    const TriangleVertexIndices* triangle_vertex_indices,
                    std::uint32_t vertex_count,
                    const std::vector<std::uint32_t>& triangle_color_counts)
{
    CUDA_CHECK(cudaMemset(normals, 0, sizeof(float4) * vertex_count));

    std::uint32_t triangle_offset = 0;
    for (std::uint32_t triangle_count : triangle_color_counts) {
        accumulate_vertex_normals_kernel<<<block_count(triangle_count, block_size), block_size>>>(
            positions,
            triangle_vertex_indices + triangle_offset,
            triangle_count,
            normals);
        CUDA_CHECK(cudaGetLastError());
        triangle_offset += triangle_count;
    }

    normalize_vertex_normals_kernel<<<block_count(vertex_count, block_size), block_size>>>(vertex_count, normals);
    CUDA_CHECK(cudaGetLastError());
}
