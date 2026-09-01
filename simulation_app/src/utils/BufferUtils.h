#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/scene/SimulationBufferBindings.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

template <typename T>
constexpr GLsizeiptr byte_size(std::size_t count) noexcept
{
    return static_cast<GLsizeiptr>(count * sizeof(T));
}

inline bool is_valid_buffer_access(std::uint32_t start_index, std::uint32_t count, std::uint32_t total_count)
{
    return count != 0u && start_index <= total_count && count <= total_count - start_index;
}

inline bool has_valid_distance_constraints(std::uint32_t constraint_count,
                                           const std::vector<ConstraintColorState>& color_states)
{
    return constraint_count != 0u && !color_states.empty();
}

inline bool has_collision_candidate_capacity(const CollisionCandidateBuffers& buffers)
{
    return buffers.max_pairs != 0;
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
