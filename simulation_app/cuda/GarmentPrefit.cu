#include "GarmentPrefit.h"
#include "gpu/pd/PdCharacterGpuState.h"
#include "simulation/SimulationParams.h"
#include "simulation/bvh/Bvh.h"
#include "utils/CudaUtils.h"

#include <cfloat>

#include <cuda_runtime.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr std::uint32_t block_size = 128;
constexpr std::uint32_t max_bvh_stack_depth = 32;

__global__ void prefit_garments_kernel(float4* positions,
                                       std::uint32_t vertex_count,
                                       const float4* body_positions,
                                       const BvhNode* nodes,
                                       const TriangleVertexIndices* triangle_indices,
                                       PrefitParams params)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= vertex_count) {
        return;
    }

    const glm::vec3 position = glm::make_vec3(&positions[index].x);
    float best_distance_squared = params.surface_search_radius * params.surface_search_radius;
    glm::vec3 nearest_point{0.0f};
    glm::vec3 nearest_normal{0.0f};
    bool has_surface = false;
    std::uint32_t stack[max_bvh_stack_depth];
    stack[0] = 0;
    std::uint32_t stack_count = 1;

    while (stack_count > 0) {
        const BvhNode& node = nodes[stack[--stack_count]];
        const glm::vec3 bounds_offset = position - glm::clamp(position,
                                                              glm::vec3(node.bounds.min_bounds),
                                                              glm::vec3(node.bounds.max_bounds));
        if (glm::dot(bounds_offset, bounds_offset) > best_distance_squared) {
            continue;
        }
        if (node.element_count == 0) {
            stack[stack_count++] = node.left_child_index;
            stack[stack_count++] = node.right_child_index;
            continue;
        }

        for (std::uint32_t i = 0; i < node.element_count; ++i) {
            const TriangleVertexIndices indices = triangle_indices[node.first_element_index + i];
            const glm::vec3 triangle[]{glm::make_vec3(&body_positions[indices.a].x),
                                       glm::make_vec3(&body_positions[indices.b].x),
                                       glm::make_vec3(&body_positions[indices.c].x)};
            const glm::vec3 ab = triangle[1] - triangle[0];
            const glm::vec3 ac = triangle[2] - triangle[0];
            const glm::vec3 ap = position - triangle[0];
            const glm::vec3 normal = glm::cross(ab, ac);
            const float normal_squared = glm::dot(normal, normal);
            const float v = glm::dot(glm::cross(ap, ac), normal) / normal_squared;
            const float w = glm::dot(glm::cross(ab, ap), normal) / normal_squared;
            glm::vec3 point = position - normal * (glm::dot(ap, normal) / normal_squared);

            if (v < 0.0f || w < 0.0f || v + w > 1.0f) {
                float edge_distance_squared = FLT_MAX;
                for (int start = 0; start < 3; ++start) {
                    const glm::vec3 edge = triangle[(start + 1) % 3] - triangle[start];
                    const float t = glm::clamp(glm::dot(position - triangle[start], edge) / glm::dot(edge, edge),
                                               0.0f,
                                               1.0f);
                    const glm::vec3 edge_point = triangle[start] + t * edge;
                    const glm::vec3 offset = position - edge_point;
                    const float distance_squared = glm::dot(offset, offset);
                    if (distance_squared <= edge_distance_squared) {
                        edge_distance_squared = distance_squared;
                        point = edge_point;
                    }
                }
            }

            const glm::vec3 offset = position - point;
            const float distance_squared = glm::dot(offset, offset);
            if (distance_squared <= best_distance_squared) {
                best_distance_squared = distance_squared;
                nearest_point = point;
                nearest_normal = normal * rsqrtf(normal_squared);
                has_surface = true;
            }
        }
    }

    if (has_surface && glm::dot(position - nearest_point, nearest_normal) < params.pushout_margin) {
        glm::vec3 correction = nearest_point + params.pushout_margin * nearest_normal - position;
        const float correction_length = glm::length(correction);
        if (correction_length > params.max_correction_length) {
            correction *= params.max_correction_length / correction_length;
        }
        const glm::vec3 corrected_position = position + correction;
        positions[index].x = corrected_position.x;
        positions[index].y = corrected_position.y;
        positions[index].z = corrected_position.z;
    }
}
}

void prefit_garments(float4* positions,
                     std::uint32_t vertex_count,
                     const float4* body_positions,
                     const TriangleBvhBuffers& bvh,
                     const PrefitParams& params)
{
    for (std::uint32_t iteration = 0; iteration < params.iteration_count; ++iteration) {
        prefit_garments_kernel<<<block_count(vertex_count, block_size), block_size>>>(positions,
                                                                                      vertex_count,
                                                                                      body_positions,
                                                                                      bvh.nodes,
                                                                                      bvh.triangle_vertex_indices,
                                                                                      params);
        CUDA_CHECK(cudaGetLastError());
    }
}
