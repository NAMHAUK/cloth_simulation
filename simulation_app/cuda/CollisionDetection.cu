#include "CollisionDetection.h"
#include "gpu/pd/PdGpuState.h"
#include "simulation/bvh/Bvh.h"
#include "utils/CudaUtils.h"

#include <cstdint>

#include <cuda_runtime.h>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vector_relational.hpp>

namespace {
constexpr std::uint32_t block_size = 128;
constexpr std::uint32_t max_bvh_stack_depth = 32;

__device__ bool overlaps_aabb(const Aabb& left, const Aabb& right)
{
    return glm::all(glm::lessThanEqual(glm::vec3(left.min_bounds), glm::vec3(right.max_bounds))) &&
           glm::all(glm::greaterThanEqual(glm::vec3(left.max_bounds), glm::vec3(right.min_bounds)));
}

__global__ void detect_body_collision_candidates_kernel(const float4* positions,
                                                        const float4* previous_positions,
                                                        std::uint32_t vertex_count,
                                                        const BvhNode* nodes,
                                                        const Aabb* triangle_bounds,
                                                        PdGpuState::CollisionCandidateBuffers candidates)
{
    const std::uint32_t vertex_index = blockIdx.x * blockDim.x + threadIdx.x;
    if (vertex_index >= vertex_count) {
        return;
    }

    const glm::vec3 previous_position = glm::make_vec3(&previous_positions[vertex_index].x);
    const glm::vec3 position = glm::make_vec3(&positions[vertex_index].x);
    const Aabb bounds = {glm::vec4(glm::min(previous_position, position), 0.0f),
                         glm::vec4(glm::max(previous_position, position), 0.0f)};

    std::uint32_t node_index_stack[max_bvh_stack_depth];
    node_index_stack[0] = 0;
    std::uint32_t stack_count = 1;

    while (stack_count > 0) {
        const BvhNode& node = nodes[node_index_stack[--stack_count]];
        if (!overlaps_aabb(bounds, node.bounds)) {
            continue;
        }

        // internal node
        if (node.element_count == 0) {
            node_index_stack[stack_count++] = node.left_child_index;
            node_index_stack[stack_count++] = node.right_child_index;
            continue;
        }

        // leaf node
        for (std::uint32_t i = 0; i < node.element_count; ++i) {
            const std::uint32_t triangle_index = node.first_element_index + i;
            if (!overlaps_aabb(bounds, triangle_bounds[triangle_index])) {
                continue;
            }

            const std::uint32_t candidate_index = atomicAdd(candidates.count, 1u);
            if (candidate_index < candidates.capacity) {
                candidates.pairs[candidate_index] = make_uint2(vertex_index, triangle_index);
            }
        }
    }
}
}

void detect_body_collision_candidates(const float4* positions, PdGpuState& state)
{
    const PdClothGpuState& cloth_state = state.cloth_gpu_state();
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    const TriangleBvhBuffers& bvh = state.character_gpu_state().triangle_bvh();

    CUDA_CHECK(cudaMemsetAsync(state.body_collision_candidates().count, 0, sizeof(std::uint32_t)));
    detect_body_collision_candidates_kernel<<<block_count(vertex_count, block_size), block_size>>>(
        positions,
        cloth_state.previous_position_buffer(),
        vertex_count,
        bvh.nodes,
        bvh.triangle_bounds,
        state.body_collision_candidates());
    CUDA_CHECK(cudaGetLastError());
}
