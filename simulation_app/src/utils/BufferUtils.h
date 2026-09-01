#pragma once

#include "gpu/scene/SimulationBufferBindings.h"

#include <cstddef>
#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

template <typename T>
constexpr GLsizeiptr byte_size(std::size_t count) noexcept
{
    return static_cast<GLsizeiptr>(count * sizeof(T));
}

inline void clear_collision_candidate_counts(const CollisionCandidateBuffers& buffers,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t zero_uint = 0;
    gl.glClearNamedBufferData(buffers.count_buffer, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero_uint);
}

inline void clear_collision_correction_sum(GLuint buffer, QOpenGLFunctions_4_5_Core& gl)
{
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(buffer, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, zero_int);
}
