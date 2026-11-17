#include "ClothMass.h"
#include "asset/AssetDataTypes.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>
#include <glm/geometric.hpp>
#include <thrust/device_vector.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void compute_vertex_masses_kernel(float* masses,
                                             const glm::vec3* rest_positions,
                                             const TriangleVertexIndices* triangle_vertex_indices,
                                             std::uint32_t triangle_count,
                                             float surface_density)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= triangle_count) {
        return;
    }

    const TriangleVertexIndices vertex_indices = triangle_vertex_indices[index];
    const glm::vec3 ab = rest_positions[vertex_indices.b] - rest_positions[vertex_indices.a];
    const glm::vec3 ac = rest_positions[vertex_indices.c] - rest_positions[vertex_indices.a];
    const float rest_area = 0.5f * glm::length(glm::cross(ab, ac));
    const float mass = surface_density * rest_area / 3.0f;

    atomicAdd(&masses[vertex_indices.a], mass);
    atomicAdd(&masses[vertex_indices.b], mass);
    atomicAdd(&masses[vertex_indices.c], mass);
}
}

void compute_vertex_masses(float* masses, const GarmentMesh& mesh, float surface_density)
{
    const auto triangle_count = static_cast<std::uint32_t>(mesh.triangle_vertex_indices.size());

    thrust::device_vector<glm::vec3> rest_positions(mesh.vertex_positions.data(),
                                                    mesh.vertex_positions.data() + mesh.vertex_positions.size());
    thrust::device_vector<TriangleVertexIndices> triangle_vertex_indices(
        mesh.triangle_vertex_indices.data(),
        mesh.triangle_vertex_indices.data() + triangle_count);

    compute_vertex_masses_kernel<<<block_count(triangle_count, block_size), block_size>>>(
        masses,
        rest_positions.data().get(),
        triangle_vertex_indices.data().get(),
        triangle_count,
        surface_density);
    CUDA_CHECK(cudaGetLastError());
}
