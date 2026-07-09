#include "gpu/scene/CollisionPairBuffers.h"

namespace {

constexpr std::uint32_t dispatch_component_count = 3;

bool has_collision_pair_buffer(const CollisionPairBuffer& buffers)
{
    return buffers.pairs != 0 &&
           buffers.pair_count != 0 &&
           buffers.dispatch_size != 0 &&
           buffers.overflow_count != 0 &&
           buffers.capacity != 0;
}

void create_buffer(GLuint& buffer, GLsizeiptr size, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glCreateBuffers(1, &buffer);
    gl.glNamedBufferData(buffer, size, nullptr, GL_DYNAMIC_DRAW);
}

void create_collision_pair_buffer(CollisionPairBuffer& buffers, std::uint32_t capacity, QOpenGLFunctions_4_5_Core& gl)
{
    buffers.capacity = capacity;
    create_buffer(buffers.pairs, static_cast<GLsizeiptr>(capacity * sizeof(std::uint32_t) * 2u), gl);
    create_buffer(buffers.pair_count, static_cast<GLsizeiptr>(sizeof(std::uint32_t)), gl);
    create_buffer(buffers.dispatch_size, static_cast<GLsizeiptr>(dispatch_component_count * sizeof(std::uint32_t)), gl);
    create_buffer(buffers.overflow_count, static_cast<GLsizeiptr>(sizeof(std::uint32_t)), gl);
}

void clear_collision_pair_counts(const CollisionPairBuffer& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t zero_uint = 0;
    gl.glClearNamedBufferData(buffers.pair_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              &zero_uint);
    gl.glClearNamedBufferData(buffers.overflow_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              &zero_uint);
}

void delete_collision_pair_buffer(CollisionPairBuffer& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers.pairs);
    gl.glDeleteBuffers(1, &buffers.pair_count);
    gl.glDeleteBuffers(1, &buffers.dispatch_size);
    gl.glDeleteBuffers(1, &buffers.overflow_count);
    buffers = {};
}

}

bool CollisionPairBuffers::ensure_capacity(std::uint32_t vertex_count,
                                              std::uint32_t triangle_count,
                                              std::uint32_t edge_count,
                                              QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t cloth_vertex_body_face_capacity = vertex_count * pair_capacity_multiplier;
    const std::uint32_t cloth_edge_body_edge_capacity = edge_count * pair_capacity_multiplier;
    const std::uint32_t cloth_face_body_vertex_capacity = triangle_count * pair_capacity_multiplier;

    if (has_collision_pair_buffer(buffers_.cloth_vertex_body_face) &&
        has_collision_pair_buffer(buffers_.cloth_edge_body_edge) &&
        has_collision_pair_buffer(buffers_.cloth_face_body_vertex) &&
        buffers_.correction_sum_buffer != 0 &&
        buffers_.vertex_capacity >= vertex_count &&
        buffers_.cloth_vertex_body_face.capacity >= cloth_vertex_body_face_capacity &&
        buffers_.cloth_edge_body_edge.capacity >= cloth_edge_body_edge_capacity &&
        buffers_.cloth_face_body_vertex.capacity >= cloth_face_body_vertex_capacity) {
        return true;
    }

    release(gl);

    buffers_.vertex_capacity = vertex_count;
    create_collision_pair_buffer(buffers_.cloth_vertex_body_face, cloth_vertex_body_face_capacity, gl);
    create_collision_pair_buffer(buffers_.cloth_edge_body_edge, cloth_edge_body_edge_capacity, gl);
    create_collision_pair_buffer(buffers_.cloth_face_body_vertex, cloth_face_body_vertex_capacity, gl);
    create_buffer(buffers_.correction_sum_buffer, static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u), gl);

    return has_collision_pair_buffer(buffers_.cloth_vertex_body_face) &&
           has_collision_pair_buffer(buffers_.cloth_edge_body_edge) &&
           has_collision_pair_buffer(buffers_.cloth_face_body_vertex) &&
           buffers_.correction_sum_buffer != 0;
}

void CollisionPairBufferView::clear_pair_counts(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_collision_pair_counts(cloth_vertex_body_face, gl);
    clear_collision_pair_counts(cloth_edge_body_edge, gl);
    clear_collision_pair_counts(cloth_face_body_vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CollisionPairBufferView::clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const
{
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(correction_sum_buffer,
                              GL_RGBA32I,
                              GL_RGBA_INTEGER,
                              GL_INT,
                              zero_int);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

CollisionPairBufferView CollisionPairBuffers::view() const
{
    return buffers_;
}

void CollisionPairBuffers::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_collision_pair_buffer(buffers_.cloth_vertex_body_face, gl);
    delete_collision_pair_buffer(buffers_.cloth_edge_body_edge, gl);
    delete_collision_pair_buffer(buffers_.cloth_face_body_vertex, gl);
    gl.glDeleteBuffers(1, &buffers_.correction_sum_buffer);
    buffers_ = {};
}
