#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuResources final
{
public:
    // Construction and state
    CharacterGpuResources() = default;
    CharacterGpuResources(const CharacterGpuResources&) = delete;
    CharacterGpuResources& operator=(const CharacterGpuResources&) = delete;

    bool is_initialized() const;

    // Mesh upload and playback
    void upload_motion(const CharacterMotion& character_motion,
                       const Bvh& body_triangle_bvh,
                       const Bvh& body_vertex_bvh,
                       const Bvh& body_edge_bvh,
                       QOpenGLFunctions_4_5_Core& gl);
    void set_current_frame(std::uint32_t frame_index);
    std::uint32_t current_frame_index() const;
    std::uint32_t next_frame_index() const;
    std::uint32_t frame_position_begin_index(std::uint32_t frame_index) const;
    std::uint32_t vertex_count() const;

    // Rendering
    void bind_current_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw(QOpenGLFunctions_4_5_Core& gl) const;

    // Mesh buffer resources
    CharacterMeshTopologyResources mesh_topology_resources() const;
    CharacterAnimationBufferView animation_buffer_view() const;
    CharacterVertexBufferView character_vertex_buffer_view() const;
    BodyTriangleResources body_triangle_resources() const;
    CharacterNormalResources mesh_normal_resources() const;
    BvhBufferView body_triangle_bvh_buffer_view() const;
    BvhBufferView body_vertex_bvh_buffer_view() const;
    BvhBufferView body_edge_bvh_buffer_view() const;

    // GPU resource lifetime
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool has_gpu_objects() const;
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    // Draw resources
    GLuint vao_ = 0;
    GLsizei index_count_ = 0;

    // GPU buffers
    CharacterBufferSet buffers_;

    // Animated position data
    std::uint32_t frame_count_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t current_frame_index_ = 0;

    // Mesh triangle and adjacent triangle data
    std::uint32_t triangle_count_ = 0;
    glm::uvec4 arm_triangle_ranges_{};
};
