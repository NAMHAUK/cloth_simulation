#include "gpu/scene/CollisionContactBuffers.h"

namespace {

constexpr std::uint32_t dispatch_indirect_component_count = 3;

bool has_contact_pair_buffers(const ContactPairBuffers& buffers)
{
    return buffers.pairs != 0 &&
           buffers.count != 0 &&
           buffers.dispatch_command != 0 &&
           buffers.overflow_count != 0 &&
           buffers.capacity != 0;
}

void create_contact_pair_buffers(ContactPairBuffers& buffers,
                                 std::uint32_t capacity,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    buffers.capacity = capacity;
    gl.glCreateBuffers(1, &buffers.pairs);
    gl.glCreateBuffers(1, &buffers.count);
    gl.glCreateBuffers(1, &buffers.dispatch_command);
    gl.glCreateBuffers(1, &buffers.overflow_count);

    gl.glNamedBufferData(buffers.pairs,
                         static_cast<GLsizeiptr>(capacity * sizeof(std::uint32_t) * 2u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.count,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.dispatch_command,
                         static_cast<GLsizeiptr>(dispatch_indirect_component_count * sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.overflow_count,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
}

void clear_contact_pair_counts(const ContactPairBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t zero_uint[4] = {};
    gl.glClearNamedBufferData(buffers.count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
    gl.glClearNamedBufferData(buffers.overflow_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
}

void delete_contact_pair_buffers(ContactPairBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers.pairs);
    gl.glDeleteBuffers(1, &buffers.count);
    gl.glDeleteBuffers(1, &buffers.dispatch_command);
    gl.glDeleteBuffers(1, &buffers.overflow_count);
    buffers = {};
}

}

bool CollisionContactBuffers::ensure_capacity(std::uint32_t vertex_count,
                                              std::uint32_t triangle_count,
                                              std::uint32_t edge_count,
                                              QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t cloth_vertex_body_face_capacity = vertex_count * pair_capacity_multiplier;
    const std::uint32_t cloth_edge_body_edge_capacity = edge_count * pair_capacity_multiplier;
    const std::uint32_t cloth_face_body_vertex_capacity = triangle_count * pair_capacity_multiplier;

    if (has_contact_pair_buffers(buffers_.cloth_vertex_body_face) &&
        has_contact_pair_buffers(buffers_.cloth_edge_body_edge) &&
        has_contact_pair_buffers(buffers_.cloth_face_body_vertex) &&
        buffers_.correction_sum_buffer != 0 &&
        buffers_.vertex_capacity >= vertex_count &&
        buffers_.cloth_vertex_body_face.capacity >= cloth_vertex_body_face_capacity &&
        buffers_.cloth_edge_body_edge.capacity >= cloth_edge_body_edge_capacity &&
        buffers_.cloth_face_body_vertex.capacity >= cloth_face_body_vertex_capacity) {
        return true;
    }

    release(gl);

    buffers_.vertex_capacity = vertex_count;
    create_contact_pair_buffers(buffers_.cloth_vertex_body_face, cloth_vertex_body_face_capacity, gl);
    create_contact_pair_buffers(buffers_.cloth_edge_body_edge, cloth_edge_body_edge_capacity, gl);
    create_contact_pair_buffers(buffers_.cloth_face_body_vertex, cloth_face_body_vertex_capacity, gl);
    gl.glCreateBuffers(1, &buffers_.correction_sum_buffer);
    gl.glNamedBufferData(buffers_.correction_sum_buffer,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u),
                         nullptr,
                         GL_DYNAMIC_DRAW);

    return has_contact_pair_buffers(buffers_.cloth_vertex_body_face) &&
           has_contact_pair_buffers(buffers_.cloth_edge_body_edge) &&
           has_contact_pair_buffers(buffers_.cloth_face_body_vertex) &&
           buffers_.correction_sum_buffer != 0;
}

void CollisionContactBufferView::clear_contact_counts(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_contact_pair_counts(cloth_vertex_body_face, gl);
    clear_contact_pair_counts(cloth_edge_body_edge, gl);
    clear_contact_pair_counts(cloth_face_body_vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CollisionContactBufferView::clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const
{
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(correction_sum_buffer,
                              GL_RGBA32I,
                              GL_RGBA_INTEGER,
                              GL_INT,
                              zero_int);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

CollisionContactBufferView CollisionContactBuffers::view() const
{
    return buffers_;
}

void CollisionContactBuffers::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_contact_pair_buffers(buffers_.cloth_vertex_body_face, gl);
    delete_contact_pair_buffers(buffers_.cloth_edge_body_edge, gl);
    delete_contact_pair_buffers(buffers_.cloth_face_body_vertex, gl);
    gl.glDeleteBuffers(1, &buffers_.correction_sum_buffer);
    buffers_ = {};
}
