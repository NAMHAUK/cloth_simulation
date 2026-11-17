#include "AttachmentConstraints.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/ClothGpuState.h"
#include "gpu/pd/ConstraintTypes.h"
#include "gpu/pd/CudaMeshResources.h"
#include "gpu/pd/PdConstraints.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cfloat>

#include <cuda_runtime.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thrust/device_vector.h>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void initialize_attachment_constraints_kernel(AttachmentConstraint* constraints,
                                                         const std::uint32_t* vertex_indices,
                                                         std::uint32_t constraint_count,
                                                         std::uint32_t vertex_offset,
                                                         float weight)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    AttachmentConstraint& constraint = constraints[index];
    constraint = AttachmentConstraint{};
    constraint.vertex_indices[0] = vertex_offset + vertex_indices[index];

    constraint.A[0][0] = 1.0f;
    constraint.B[0][0] = 1.0f;

    constraint.weight = weight;
}

__device__ glm::vec3 compute_closest_barycentric(const glm::vec3& point,
                                                 const glm::vec3& a,
                                                 const glm::vec3& b,
                                                 const glm::vec3& c)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = point - a;
    const glm::vec3 normal = glm::cross(ab, ac);
    const float normal_squared = glm::dot(normal, normal);
    const float v = glm::dot(glm::cross(ap, ac), normal) / normal_squared;
    const float w = glm::dot(glm::cross(ab, ap), normal) / normal_squared;
    glm::vec3 barycentric{1.0f - v - w, v, w};

    // 내부 위치면 바로 반환
    if (barycentric.x >= 0.0f && barycentric.y >= 0.0f && barycentric.z >= 0.0f) {
        return barycentric;
    }

    // 외부면 삼각형 변 중에서 가장 가까운 위치 계산
    const glm::vec3 vertices[]{a, b, c};
    const int edges[][2] = {{0, 1}, {1, 2}, {2, 0}};
    float best_distance_squared = FLT_MAX;
    for (const auto& [start, end] : edges) {
        const glm::vec3 edge = vertices[end] - vertices[start];
        const glm::vec3 offset = point - vertices[start];
        const float t = glm::clamp(glm::dot(offset, edge) / glm::dot(edge, edge), 0.0f, 1.0f);
        const glm::vec3 delta = t * edge - offset;
        const float distance_squared = glm::dot(delta, delta);
        if (distance_squared < best_distance_squared) {
            best_distance_squared = distance_squared;
            barycentric = glm::vec3(0.0f);
            barycentric[start] = 1.0f - t;
            barycentric[end] = t;
        }
    }
    return barycentric;
}

__global__ void find_attachment_targets_kernel(AttachmentConstraint* constraints,
                                               std::uint32_t constraint_count,
                                               const float4* cloth_positions,
                                               MappedMeshBuffers character_buffers,
                                               std::uint32_t body_triangle_count)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    AttachmentConstraint& constraint = constraints[index];
    const glm::vec3 point = glm::make_vec3(&cloth_positions[constraint.vertex_indices[0]].x);
    constraint.body_triangle_index = UINT32_MAX;
    float best_distance_squared = FLT_MAX;

    for (std::uint32_t triangle_index = 0; triangle_index < body_triangle_count; ++triangle_index) {
        const TriangleVertexIndices triangle = character_buffers.triangle_vertex_indices[triangle_index];
        const glm::vec3 a = glm::make_vec3(&character_buffers.positions[triangle.a].x);
        const glm::vec3 b = glm::make_vec3(&character_buffers.positions[triangle.b].x);
        const glm::vec3 c = glm::make_vec3(&character_buffers.positions[triangle.c].x);
        if (!(glm::length(glm::cross(b - a, c - a)) > 1.0e-10f)) {
            continue;
        }

        const glm::vec3 barycentric = compute_closest_barycentric(point, a, b, c);
        const glm::vec3 delta = barycentric.x * a + barycentric.y * b + barycentric.z * c - point;
        const float distance_squared = glm::dot(delta, delta);
        if (distance_squared < best_distance_squared) {
            best_distance_squared = distance_squared;
            constraint.body_triangle_index = triangle_index;
            constraint.barycentric = make_float3(barycentric.x, barycentric.y, barycentric.z);
        }
    }
}

__global__ void project_attachment_constraints_kernel(AttachmentConstraint* constraints,
                                                      std::uint32_t constraint_count,
                                                      const float4* body_positions,
                                                      const TriangleVertexIndices* body_triangles,
                                                      float surface_offset)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    AttachmentConstraint& constraint = constraints[index];
    const TriangleVertexIndices triangle = body_triangles[constraint.body_triangle_index];
    const glm::vec3 a = glm::make_vec3(&body_positions[triangle.a].x);
    const glm::vec3 b = glm::make_vec3(&body_positions[triangle.b].x);
    const glm::vec3 c = glm::make_vec3(&body_positions[triangle.c].x);
    glm::vec3 target = constraint.barycentric.x * a + constraint.barycentric.y * b + constraint.barycentric.z * c;
    const glm::vec3 normal = glm::cross(b - a, c - a);
    const float normal_length = glm::length(normal);
    if (normal_length > 1.0e-10f) {
        target += surface_offset * (normal / normal_length);
    }
    constraint.p[0] = make_float3(target.x, target.y, target.z);
}
}

void initialize_constraints(AttachmentConstraint* constraints,
                            const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state)
{
    std::uint32_t constraint_offset = 0;
    for (const GarmentObject& garment : garments) {
        const auto constraint_count = static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());
        if (constraint_count == 0) {
            continue;
        }

        thrust::device_vector<std::uint32_t> vertex_indices(garment.mesh.attachment_vertex_indices);
        initialize_attachment_constraints_kernel<<<block_count(constraint_count, block_size), block_size>>>(
            constraints + constraint_offset,
            vertex_indices.data().get(),
            constraint_count,
            cloth_state.garment_states()[garment.layer].vertex_start_index,
            garment.attachment_weight);
        CUDA_CHECK(cudaGetLastError());
        
        constraint_offset += constraint_count;
    }
}

void find_attachment_targets(const ConstraintSet<AttachmentConstraint>& constraints,
                             const float4* cloth_positions,
                             const MappedMeshBuffers& character_buffers,
                             std::uint32_t body_triangle_count)
{
    if (constraints.count == 0) {
        return;
    }
    find_attachment_targets_kernel<<<block_count(constraints.count, block_size), block_size>>>(constraints.buffer,
                                                                                               constraints.count,
                                                                                               cloth_positions,
                                                                                               character_buffers,
                                                                                               body_triangle_count);
    CUDA_CHECK(cudaGetLastError());
}

void project_constraints(const ConstraintSet<AttachmentConstraint>& constraints,
                         const float4* body_positions,
                         const TriangleVertexIndices* body_triangles,
                         float surface_offset)
{
    if (constraints.count == 0) {
        return;
    }
    project_attachment_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(
        constraints.buffer,
        constraints.count,
        body_positions,
        body_triangles,
        surface_offset);
    CUDA_CHECK(cudaGetLastError());
}
