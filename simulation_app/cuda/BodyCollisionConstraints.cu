#include "BodyCollisionConstraints.h"
#include "gpu/pd/PdConstraints.h"
#include "utils/CudaUtils.h"

#include <cuda_runtime.h>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr std::uint32_t block_size = 256;

__global__ void project_body_collision_constraints_kernel(BodyCollisionConstraint* constraints,
                                                          std::uint32_t constraint_count,
                                                          const float4* positions)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= constraint_count) {
        return;
    }

    BodyCollisionConstraint& constraint = constraints[index];
    const glm::vec3 position = glm::make_vec3(&positions[constraint.vertex_indices[0]].x);
    const glm::vec3 normal = glm::make_vec3(&constraint.normal.x);
    const float distance = glm::dot(normal, position - glm::make_vec3(&constraint.contact_point.x));
    const glm::vec3 projection = position + fmaxf(0.0f, body_collision_thickness - distance) * normal;
    constraint.p[0] = make_float3(projection.x, projection.y, projection.z);
}
}

void project_constraints(const ConstraintSet<BodyCollisionConstraint>& constraints, const float4* positions)
{
    if (constraints.count == 0) {
        return;
    }

    project_body_collision_constraints_kernel<<<block_count(constraints.count, block_size), block_size>>>(
        constraints.buffer,
        constraints.count,
        positions);
    CUDA_CHECK(cudaGetLastError());
}
