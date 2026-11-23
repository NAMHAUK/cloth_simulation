#include "gpu/pd/PdCharacterGpuState.h"
#include "asset/AssetDataTypes.h"
#include "gpu/common/CharacterGpuState.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cstddef>

#include <cuda_runtime.h>

// Initialization
void PdCharacterGpuState::initialize(const SceneState& scene, const CharacterGpuState& character_state)
{
    vertex_count_ = character_state.vertex_count();

    shared_buffers.register_buffers(character_state.buffer_set().current_position,
                                    character_state.buffer_set().vertex_normal,
                                    character_state.buffer_set().triangle_vertex_indices);

    const TriangleBvh& body_triangle_bvh = scene.default_body_triangle_bvh();
    triangle_bvh_.level_offsets = body_triangle_bvh.level_offsets;

    const std::size_t node_bytes = sizeof(BvhNode) * body_triangle_bvh.nodes.size();
    CUDA_CHECK(cudaMalloc(&triangle_bvh_.nodes, node_bytes));
    CUDA_CHECK(cudaMemcpy(triangle_bvh_.nodes, body_triangle_bvh.nodes.data(), node_bytes, cudaMemcpyHostToDevice));

    const std::size_t index_bytes = sizeof(TriangleVertexIndices) * body_triangle_bvh.triangle_vertex_indices.size();
    CUDA_CHECK(cudaMalloc(&triangle_bvh_.triangle_vertex_indices, index_bytes));
    CUDA_CHECK(cudaMemcpy(triangle_bvh_.triangle_vertex_indices,
                          body_triangle_bvh.triangle_vertex_indices.data(),
                          index_bytes,
                          cudaMemcpyHostToDevice));

    CUDA_CHECK(cudaMalloc(&triangle_bvh_.triangle_bounds, sizeof(Aabb) * body_triangle_bvh.leaf_element_count()));
}

// Motion
void PdCharacterGpuState::set_motion(const CharacterMotion& motion)
{
    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;

    const std::size_t position_bytes = sizeof(float4) * motion.vertex_positions.size();
    CUDA_CHECK(cudaMalloc(&animation_position_buffer_, position_bytes));
    CUDA_CHECK(
        cudaMemcpy(animation_position_buffer_, motion.vertex_positions.data(), position_bytes, cudaMemcpyHostToDevice));
}

// Accessors
const float4* PdCharacterGpuState::animation_position_buffer() const
{
    return animation_position_buffer_;
}

const TriangleBvhBuffers& PdCharacterGpuState::triangle_bvh() const
{
    return triangle_bvh_;
}

std::uint32_t PdCharacterGpuState::vertex_count() const
{
    return vertex_count_;
}

// Release
void PdCharacterGpuState::release()
{
    shared_buffers.release();

    CUDA_CHECK(cudaFree(triangle_bvh_.nodes));
    CUDA_CHECK(cudaFree(triangle_bvh_.triangle_vertex_indices));
    CUDA_CHECK(cudaFree(triangle_bvh_.triangle_bounds));
    triangle_bvh_ = {};

    CUDA_CHECK(cudaFree(animation_position_buffer_));
    animation_position_buffer_ = nullptr;
    vertex_count_ = 0;
}
