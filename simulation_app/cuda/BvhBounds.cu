#include "BvhBounds.h"
#include "gpu/pd/PdCharacterGpuState.h"
#include "simulation/bvh/Bvh.h"
#include "simulation/pd/PdSimulationParams.h"
#include "utils/CudaUtils.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr std::uint32_t block_size = 128;
constexpr float body_detection_distance = PdSimulationParams::body_detection_distance;

__global__ void update_triangle_bvh_bounds_kernel(BvhNode* nodes,
                                                  const TriangleVertexIndices* triangle_vertex_indices,
                                                  Aabb* triangle_bounds,
                                                  const float4* positions,
                                                  std::uint32_t first_node_index,
                                                  std::uint32_t node_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= node_count) {
        return;
    }

    BvhNode& node = nodes[first_node_index + index];
    const bool is_leaf = node.element_count > 0;
    if (is_leaf) {
        node.bounds = {glm::vec4(glm::vec3(FLT_MAX), 0.0f), glm::vec4(glm::vec3(-FLT_MAX), 0.0f)};

        for (std::uint32_t i = 0; i < node.element_count; ++i) {
            const std::uint32_t triangle_index = node.first_element_index + i;
            const TriangleVertexIndices indices = triangle_vertex_indices[triangle_index];
            const glm::vec3 a = glm::make_vec3(&positions[indices.a].x);
            const glm::vec3 b = glm::make_vec3(&positions[indices.b].x);
            const glm::vec3 c = glm::make_vec3(&positions[indices.c].x);

            Aabb& bounds = triangle_bounds[triangle_index];
            bounds.min_bounds = glm::vec4(glm::min(a, glm::min(b, c)) - body_detection_distance, 0.0f);
            bounds.max_bounds = glm::vec4(glm::max(a, glm::max(b, c)) + body_detection_distance, 0.0f);
            node.bounds.min_bounds = glm::min(node.bounds.min_bounds, bounds.min_bounds);
            node.bounds.max_bounds = glm::max(node.bounds.max_bounds, bounds.max_bounds);
        }
    } else {
        const Aabb& left_bounds = nodes[node.left_child_index].bounds;
        const Aabb& right_bounds = nodes[node.right_child_index].bounds;
        node.bounds.min_bounds = glm::min(left_bounds.min_bounds, right_bounds.min_bounds);
        node.bounds.max_bounds = glm::max(left_bounds.max_bounds, right_bounds.max_bounds);
    }
}
}

void update_triangle_bvh_bounds(const TriangleBvhBuffers& bvh, const float4* positions)
{
    const int level_count = static_cast<int>(bvh.level_offsets.size()) - 1;
    for (int level = level_count - 1; level >= 0; --level) {
        const std::uint32_t first_node_index = bvh.level_offsets[level];
        const std::uint32_t node_count = bvh.level_offsets[level + 1] - first_node_index;

        update_triangle_bvh_bounds_kernel<<<block_count(node_count, block_size), block_size>>>(
            bvh.nodes,
            bvh.triangle_vertex_indices,
            bvh.triangle_bounds,
            positions,
            first_node_index,
            node_count);
        CUDA_CHECK(cudaGetLastError());
    }
}
