#pragma once

#include "io/MotionAsset.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuResources final {
public:
    CharacterGpuResources() = default;
    CharacterGpuResources(const CharacterGpuResources&) = delete;
    CharacterGpuResources& operator=(const CharacterGpuResources&) = delete;

    bool is_initialized() const;

    void upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl);
    void set_current_frame(std::uint32_t frame_index);
    void bind_animation_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    std::uint32_t current_frame_index() const;
    std::uint32_t vertex_count() const;
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    GLuint vao_ = 0;
    GLuint all_frame_vertex_buffer_ = 0;
    GLuint index_buffer_ = 0;
    GLsizeiptr all_frame_vertex_buffer_size_ = 0;
    GLsizeiptr index_buffer_size_ = 0;
    std::uint32_t frame_count_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t current_frame_index_ = 0;
    GLsizei index_count_ = 0;
};
