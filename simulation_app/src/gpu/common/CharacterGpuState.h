#pragma once

#include <QOpenGLFunctions_4_5_Core>
#include <cstdint>

class SceneState;

struct CharacterRenderBuffers final
{
    GLuint current_position = 0;
    GLuint triangle_vertex_indices = 0;
    GLuint vertex_normal = 0;
};

class CharacterGpuState final
{
public:
    CharacterGpuState() = default;
    CharacterGpuState(const CharacterGpuState&) = delete;
    CharacterGpuState& operator=(const CharacterGpuState&) = delete;

    void initialize(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;

    std::uint32_t vertex_count() const;
    std::uint32_t triangle_count() const;
    const CharacterRenderBuffers& buffer_set() const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void reset_resources() noexcept;

    GLuint vao_ = 0;
    GLsizei index_count_ = 0;
    CharacterRenderBuffers buffers_;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t triangle_count_ = 0;
};
