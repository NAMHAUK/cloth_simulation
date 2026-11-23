#include "BodyCollisionConstraints.h"
#include "CollisionDetection.h"
#include "gpu/pd/PdGpuState.h"
#include "simulation/bvh/Bvh.h"
#include "utils/CudaUtils.h"

#include <cfloat>
#include <cstdint>

#include <cuda_runtime.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vector_relational.hpp>

namespace {
constexpr std::uint32_t block_size = 128;
constexpr std::uint32_t max_bvh_stack_depth = 32;
constexpr float body_collision_weight = 1000.0f;

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

__device__ glm::vec3 compute_barycentric(const glm::vec3& point, const glm::vec3* triangle)
{
    const glm::vec3 ab = triangle[1] - triangle[0];
    const glm::vec3 ac = triangle[2] - triangle[0];
    const glm::vec3 ap = point - triangle[0];
    const float d00 = glm::dot(ab, ab);
    const float d01 = glm::dot(ab, ac);
    const float d11 = glm::dot(ac, ac);
    const float d20 = glm::dot(ap, ab);
    const float d21 = glm::dot(ap, ac);
    const float denominator = d00 * d11 - d01 * d01;

    glm::vec3 barycentric;
    barycentric.y = (d11 * d20 - d01 * d21) / denominator;
    barycentric.z = (d00 * d21 - d01 * d20) / denominator;
    barycentric.x = 1.0f - barycentric.y - barycentric.z;
    return barycentric;
}

__device__ float find_thickness_contact_time(const glm::vec3& offset, const glm::vec3& displacement, float thickness)
{
    const float c = glm::dot(offset, offset) - thickness * thickness;
    if (c <= 0.0f) {
        return 0.0f;
    }

    const float a = glm::dot(displacement, displacement);
    const float b = glm::dot(offset, displacement);
    const float discriminant = b * b - a * c;
    if (a <= 0.0f || b >= 0.0f || discriminant < 0.0f) {
        return FLT_MAX;
    }

    const float time = c / (sqrtf(discriminant) - b);
    return time <= 1.0f ? time : FLT_MAX;
}

__global__ void detect_body_contacts_kernel(const float4* positions,
                                            const float4* previous_positions,
                                            const float4* body_positions,
                                            const TriangleVertexIndices* triangle_indices,
                                            const uint2* candidates,
                                            std::uint32_t candidate_count,
                                            BodyCollisionConstraint* constraints,
                                            std::uint32_t* constraint_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= candidate_count) {
        return;
    }

    const uint2 pair = candidates[index];
    const TriangleVertexIndices indices = triangle_indices[pair.y];
    const glm::vec3 triangle[]{glm::make_vec3(&body_positions[indices.a].x),
                               glm::make_vec3(&body_positions[indices.b].x),
                               glm::make_vec3(&body_positions[indices.c].x)};
    glm::vec3 normal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    normal *= rsqrtf(glm::dot(normal, normal));

    const glm::vec3 position = glm::make_vec3(&positions[pair.x].x);
    const glm::vec3 previous_position = glm::make_vec3(&previous_positions[pair.x].x);
    const float distance = glm::dot(position - triangle[0], normal);
    const float previous_distance = glm::dot(previous_position - triangle[0], normal);
    if (distance >= body_collision_thickness || fmaxf(previous_distance, distance) < -body_collision_thickness) {
        return;
    }

    const glm::vec3 displacement = position - previous_position;
    const float entry_distance = glm::clamp(previous_distance, -body_collision_thickness, body_collision_thickness);
    float entry_time = 0.0f;
    if (fabsf(previous_distance) > body_collision_thickness) {
        entry_time = (entry_distance - previous_distance) / (distance - previous_distance);
    }

    const glm::vec3 projected_position = previous_position + displacement * entry_time - normal * entry_distance;
    const glm::vec3 barycentric = compute_barycentric(projected_position, triangle);
    bool has_contact = glm::all(glm::greaterThanEqual(barycentric, glm::vec3(0.0f)));
    for (int start = 0; start < 3 && !has_contact; ++start) {
        if (barycentric[(start + 2) % 3] >= 0.0f) {
            continue;
        }

        const glm::vec3 edge = triangle[(start + 1) % 3] - triangle[start];
        const glm::vec3 offset = previous_position - triangle[start];
        const float edge_length_squared = glm::dot(edge, edge);
        const float initial_edge_fraction = glm::dot(offset, edge) / edge_length_squared;
        const float edge_fraction_delta = glm::dot(displacement, edge) / edge_length_squared;
        float time = find_thickness_contact_time(offset - edge * initial_edge_fraction,
                                                 displacement - edge * edge_fraction_delta,
                                                 body_collision_thickness);
        if (time == FLT_MAX) {
            continue;
        }

        const float edge_fraction = initial_edge_fraction + edge_fraction_delta * time;
        if (edge_fraction < 0.0f || edge_fraction > 1.0f) {
            const glm::vec3 endpoint = edge_fraction < 0.0f ? triangle[start] : triangle[(start + 1) % 3];
            time = find_thickness_contact_time(previous_position - endpoint, displacement, body_collision_thickness);
        }
        has_contact = time < FLT_MAX;
    }

    if (!has_contact && distance >= -body_collision_thickness) {
        const glm::vec3 current_barycentric = compute_barycentric(position - normal * distance, triangle);
        has_contact = glm::all(glm::greaterThanEqual(current_barycentric, glm::vec3(0.0f)));
    }

    if (has_contact) {
        BodyCollisionConstraint& constraint = constraints[atomicAdd(constraint_count, 1u)];
        constraint = BodyCollisionConstraint{};
        constraint.vertex_indices[0] = pair.x;
        constraint.contact_point = make_float3(triangle[0].x, triangle[0].y, triangle[0].z);
        constraint.normal = make_float3(normal.x, normal.y, normal.z);
        constraint.A[0][0] = 1.0f;
        constraint.B[0][0] = 1.0f;
        constraint.weight = body_collision_weight;
        constraint.rhs_matrix[0][0] = body_collision_weight;
    }
}
}

void detect_body_collisions(const float4* positions, const float4* body_positions, PdGpuState& state)
{
    const PdClothGpuState& cloth_state = state.cloth_gpu_state();
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    const TriangleBvhBuffers& bvh = state.character_gpu_state().triangle_bvh();
    CUDA_CHECK(cudaMemsetAsync(state.body_collision_count_buffer(), 0, sizeof(std::uint32_t)));
    std::uint32_t candidate_count = 0;

    // Detect collision candidates
    while (true) {
        CUDA_CHECK(cudaMemsetAsync(state.body_collision_candidates().count, 0, sizeof(std::uint32_t)));
        detect_body_collision_candidates_kernel<<<block_count(vertex_count, block_size), block_size>>>(
            positions,
            cloth_state.previous_position_buffer(),
            vertex_count,
            bvh.nodes,
            bvh.triangle_bounds,
            state.body_collision_candidates());
        CUDA_CHECK(cudaGetLastError());

        CUDA_CHECK(cudaMemcpy(&candidate_count,
                              state.body_collision_candidates().count,
                              sizeof(candidate_count),
                              cudaMemcpyDeviceToHost));
        if (candidate_count <= state.body_collision_candidates().capacity) {
            break;
        }

        state.resize_body_collision_candidates(candidate_count);
    }

    auto& constraints = state.constraints().get<ConstraintIndex::BodyCollision>();
    constraints.count = 0;
    if (candidate_count == 0) {
        return;
    }

    // Build body collision constraints
    constraints.reserve(candidate_count);
    detect_body_contacts_kernel<<<block_count(candidate_count, block_size), block_size>>>(
        positions,
        cloth_state.previous_position_buffer(),
        body_positions,
        bvh.triangle_vertex_indices,
        state.body_collision_candidates().pairs,
        candidate_count,
        constraints.buffer,
        state.body_collision_count_buffer());
    CUDA_CHECK(cudaGetLastError());

    CUDA_CHECK(cudaMemcpy(&constraints.count,
                          state.body_collision_count_buffer(),
                          sizeof(constraints.count),
                          cudaMemcpyDeviceToHost));
    state.constraints().validate_constraint_count();
}
