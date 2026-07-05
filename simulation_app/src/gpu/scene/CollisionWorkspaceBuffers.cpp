#include "gpu/scene/CollisionWorkspaceBuffers.h"

#include <algorithm>

bool CollisionWorkspaceBuffers::ensure_capacity(std::uint32_t vertex_count,
                                                std::uint32_t triangle_count,
                                                std::uint32_t edge_count,
                                                std::uint32_t max_contacts_per_vertex,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t pair_capacity = triangle_count * pair_capacity_multiplier;
    const std::uint32_t edge_pair_capacity = edge_count * pair_capacity_multiplier;
    const std::uint32_t contact_candidate_capacity = std::max(pair_capacity * 3u, edge_pair_capacity * 2u);
    if (buffers_.pair_record_buffer != 0 &&
        buffers_.vertex_capacity >= vertex_count &&
        buffers_.pair_capacity >= pair_capacity &&
        buffers_.edge_pair_capacity >= edge_pair_capacity &&
        buffers_.contact_candidate_capacity >= contact_candidate_capacity) {
        return true;
    }

    release(gl);

    buffers_.vertex_capacity = vertex_count;
    buffers_.pair_capacity = pair_capacity;
    buffers_.edge_pair_capacity = edge_pair_capacity;
    buffers_.contact_candidate_capacity = contact_candidate_capacity;

    gl.glCreateBuffers(1, &buffers_.pair_record_buffer);
    gl.glCreateBuffers(1, &buffers_.pair_count_buffer);
    gl.glCreateBuffers(1, &buffers_.edge_pair_record_buffer);
    gl.glCreateBuffers(1, &buffers_.edge_pair_count_buffer);
    gl.glCreateBuffers(1, &buffers_.correction_sum_buffer);
    gl.glCreateBuffers(1, &buffers_.contact_candidate_head_buffer);
    gl.glCreateBuffers(1, &buffers_.contact_candidate_count_buffer);
    gl.glCreateBuffers(1, &buffers_.contact_candidate_buffer);
    gl.glCreateBuffers(1, &buffers_.contact_candidate_next_buffer);

    gl.glNamedBufferData(buffers_.pair_record_buffer,
                         static_cast<GLsizeiptr>(pair_capacity * sizeof(std::uint32_t) * 2u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.pair_count_buffer,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.edge_pair_record_buffer,
                         static_cast<GLsizeiptr>(edge_pair_capacity * sizeof(std::uint32_t) * 2u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.edge_pair_count_buffer,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.correction_sum_buffer,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.contact_candidate_head_buffer,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.contact_candidate_count_buffer,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.contact_candidate_buffer,
                         static_cast<GLsizeiptr>(contact_candidate_capacity * sizeof(float) * 4u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.contact_candidate_next_buffer,
                         static_cast<GLsizeiptr>(contact_candidate_capacity * sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    return buffers_.pair_record_buffer != 0 &&
           buffers_.pair_count_buffer != 0 &&
           buffers_.edge_pair_record_buffer != 0 &&
           buffers_.edge_pair_count_buffer != 0 &&
           buffers_.correction_sum_buffer != 0 &&
           buffers_.contact_candidate_head_buffer != 0 &&
           buffers_.contact_candidate_count_buffer != 0 &&
           buffers_.contact_candidate_buffer != 0 &&
           buffers_.contact_candidate_next_buffer != 0;
}

void CollisionWorkspaceBufferView::clear(QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t zero_uint[4] = {};
    const std::uint32_t invalid_uint[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(pair_count_buffer,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
    gl.glClearNamedBufferData(edge_pair_count_buffer,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
    gl.glClearNamedBufferData(correction_sum_buffer,
                              GL_RGBA32I,
                              GL_RGBA_INTEGER,
                              GL_INT,
                              zero_int);
    gl.glClearNamedBufferData(contact_candidate_head_buffer,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              invalid_uint);
    gl.glClearNamedBufferData(contact_candidate_count_buffer,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
}

CollisionWorkspaceBufferView CollisionWorkspaceBuffers::view() const
{
    return buffers_;
}

void CollisionWorkspaceBuffers::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.pair_record_buffer);
    gl.glDeleteBuffers(1, &buffers_.pair_count_buffer);
    gl.glDeleteBuffers(1, &buffers_.edge_pair_record_buffer);
    gl.glDeleteBuffers(1, &buffers_.edge_pair_count_buffer);
    gl.glDeleteBuffers(1, &buffers_.correction_sum_buffer);
    gl.glDeleteBuffers(1, &buffers_.contact_candidate_head_buffer);
    gl.glDeleteBuffers(1, &buffers_.contact_candidate_count_buffer);
    gl.glDeleteBuffers(1, &buffers_.contact_candidate_buffer);
    gl.glDeleteBuffers(1, &buffers_.contact_candidate_next_buffer);
    buffers_ = {};
}
