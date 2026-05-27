#pragma once

#include "asset/MotionAsset.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct MeshNormalResources;
struct MeshTopologyResources;

class CharacterGpuResources final {
public:
    // Construction and state
    CharacterGpuResources() = default;
    CharacterGpuResources(const CharacterGpuResources&) = delete;
    CharacterGpuResources& operator=(const CharacterGpuResources&) = delete;

    bool is_initialized() const;

    // Mesh upload and playback
    void upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl);
    void set_current_frame(std::uint32_t frame_index);
    std::uint32_t current_frame_index() const;
    std::uint32_t vertex_count() const;

    // Rendering
    void bind_animation_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw(QOpenGLFunctions_4_5_Core& gl) const;

    // Mesh buffer resources
    MeshTopologyResources mesh_topology_resources() const;
    MeshNormalResources mesh_normal_resources() const;

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

    // Mesh adjacency data
    GLuint index_buffer_ = 0;
    std::uint32_t triangle_count_ = 0;

    // Normal update buffers
    GLuint adjacency_offset_buffer_ = 0;
    GLuint adjacency_triangle_buffer_ = 0;
    GLuint triangle_normal_buffer_ = 0;
    GLuint vertex_normal_buffer_ = 0;
};
