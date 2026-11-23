#pragma once

#include "gpu/pd/CudaMeshResources.h"

#include <cstdint>
#include <vector>

#include <vector_types.h>

class CharacterGpuState;
class SceneState;
struct Aabb;
struct BvhNode;
struct CharacterMotion;
struct TriangleVertexIndices;

struct TriangleBvhBuffers final
{
    BvhNode* nodes = nullptr;
    TriangleVertexIndices* triangle_vertex_indices = nullptr;
    Aabb* triangle_bounds = nullptr;
    std::vector<std::uint32_t> level_offsets;
};

class PdCharacterGpuState final
{
public:
    PdCharacterGpuState() = default;
    PdCharacterGpuState(const PdCharacterGpuState&) = delete;
    PdCharacterGpuState& operator=(const PdCharacterGpuState&) = delete;

    void initialize(const SceneState& scene, const CharacterGpuState& character_state);
    void set_motion(const CharacterMotion& motion);

    const float4* animation_position_buffer() const;
    const TriangleBvhBuffers& triangle_bvh() const;
    std::uint32_t vertex_count() const;

    void release();

    CudaMeshResources shared_buffers;

private:
    TriangleBvhBuffers triangle_bvh_;
    float4* animation_position_buffer_ = nullptr;
    std::uint32_t vertex_count_ = 0;
};
