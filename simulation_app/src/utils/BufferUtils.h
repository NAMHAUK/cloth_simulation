#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <QOpenGLFunctions_4_5_Core>

// Buffer creation
template <typename T>
constexpr GLsizeiptr byte_size(std::size_t count) noexcept
{
    return static_cast<GLsizeiptr>(count * sizeof(T));
}

inline GLuint create_buffer(GLsizeiptr size, const void* data, GLenum usage, QOpenGLFunctions_4_5_Core& gl)
{
    GLuint buffer = 0;
    gl.glCreateBuffers(1, &buffer);
    gl.glNamedBufferData(buffer, size, data, usage);
    return buffer;
}

inline GLuint create_dummy_buffer(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr std::array<std::uint32_t, 4> dummy_data{};
    return create_buffer(sizeof(dummy_data), dummy_data.data(), GL_STATIC_DRAW, gl);
}

// Buffer bindings
namespace common_buffer_binding {
constexpr GLuint cloth_vertex_normals = 0;
constexpr GLuint character_current_positions = 1;
constexpr GLuint character_vertex_normals = 2;
}

inline void require_ssbo_bindings(GLint required_count, QOpenGLFunctions_4_5_Core& gl)
{
    GLint max_bindings = 0;
    gl.glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &max_bindings);
    if (max_bindings < required_count) {
        throw std::runtime_error("Insufficient SSBO bindings: required=" +
                                 std::to_string(required_count) +
                                 ", reported=" +
                                 std::to_string(max_bindings) +
                                 '.');
    }
}

template <std::size_t Size>
void bind_buffers(GLuint first, const std::array<GLuint, Size>& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    if (std::find(buffers.begin(), buffers.end(), GLuint{0}) != buffers.end()) {
        throw std::runtime_error("Cannot bind a missing buffer.");
    }

    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, first, buffers.size(), buffers.data());
}

template <std::size_t Count>
void bind_dummy_buffers(GLuint first, GLuint dummy_buffer, QOpenGLFunctions_4_5_Core& gl)
{
    std::array<GLuint, Count> buffers;
    buffers.fill(dummy_buffer);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, first, buffers.size(), buffers.data());
}

// Buffer clear
inline void clear_collision_candidate_counts(GLuint buffer, QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t zero_uint = 0;
    gl.glClearNamedBufferData(buffer, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero_uint);
}

inline void clear_collision_correction_sum(GLuint buffer, QOpenGLFunctions_4_5_Core& gl)
{
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(buffer, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, zero_int);
}
