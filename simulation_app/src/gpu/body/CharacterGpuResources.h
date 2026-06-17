#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/collision/BvhDataTypes.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuResources final {
public:
    // Construction and state
    CharacterGpuResources() = default;
    CharacterGpuResources(const CharacterGpuResources&) = delete;
    CharacterGpuResources& operator=(const CharacterGpuResources&) = delete;

    bool is_initialized() const;

    // Mesh upload and playback
    void upload_mesh(const CharacterMesh& character_mesh,
                     const MeshBvhData& default_character_bvh_data,
                     QOpenGLFunctions_4_5_Core& gl);
    void set_current_frame(std::uint32_t frame_index);
    std::uint32_t current_frame_index() const;
    std::uint32_t frame_position_begin_index(std::uint32_t frame_index) const;
    std::uint32_t vertex_count() const;

    // Rendering
    void bind_animation_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw(QOpenGLFunctions_4_5_Core& gl) const;

    // Mesh buffer resources
    CharacterMeshTopologyResources mesh_topology_resources() const;
    TriangleGeometryResources character_triangle_geometry_resources() const;
    CharacterNormalResources mesh_normal_resources() const;
    MeshBvhResources character_bvh_resources() const;

    // GPU resource lifetime
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool has_gpu_objects() const;
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    // Draw resources
    GLuint vao_ = 0;
    GLsizei index_count_ = 0;

    // Animated position data
    GLuint all_frame_vertex_buffer_ = 0;
    std::uint32_t frame_count_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t current_frame_index_ = 0;

    // Mesh adjacent triangle data
    GLuint index_buffer_ = 0;
    std::uint32_t triangle_count_ = 0;

    // Character BVH data
    GLuint character_bvh_node_buffer_ = 0;
    std::uint32_t bvh_node_count_ = 0;
    std::uint32_t bvh_root_node_index_ = 0;

    // Normal update buffers
    GLuint adjacent_triangle_offsets_ = 0;
    GLuint adjacent_triangle_indices_ = 0;
    GLuint character_triangle_geometry_buffer_ = 0;
    GLuint vertex_normal_buffer_ = 0;
};
