#include "gpu/scene/CollisionWorkspaceBuffers.h"

bool CollisionWorkspaceBuffers::ensure_capacity(std::uint32_t vertex_count,
                                                std::uint32_t triangle_count,
                                                std::uint32_t edge_count,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t pair_capacity = triangle_count * pair_capacity_multiplier;
    const std::uint32_t edge_pair_capacity = edge_count * pair_capacity_multiplier;
    if (buffers_.pair_record_buffer != 0 &&
        buffers_.vertex_capacity >= vertex_count &&
        buffers_.pair_capacity >= pair_capacity &&
        buffers_.edge_pair_capacity >= edge_pair_capacity) {
        return true;
    }

    release(gl);

    buffers_.vertex_capacity = vertex_count;
    buffers_.pair_capacity = pair_capacity;
    buffers_.edge_pair_capacity = edge_pair_capacity;

    gl.glCreateBuffers(1, &buffers_.pair_record_buffer);
    gl.glCreateBuffers(1, &buffers_.pair_count_buffer);
    gl.glCreateBuffers(1, &buffers_.edge_pair_record_buffer);
    gl.glCreateBuffers(1, &buffers_.edge_pair_count_buffer);
    gl.glCreateBuffers(1, &buffers_.correction_sum_buffer);

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
    return buffers_.pair_record_buffer != 0 &&
           buffers_.pair_count_buffer != 0 &&
           buffers_.edge_pair_record_buffer != 0 &&
           buffers_.edge_pair_count_buffer != 0 &&
           buffers_.correction_sum_buffer != 0;
}

void CollisionWorkspaceBufferView::clear(QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t zero_uint[4] = {};
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
    buffers_ = {};
}
