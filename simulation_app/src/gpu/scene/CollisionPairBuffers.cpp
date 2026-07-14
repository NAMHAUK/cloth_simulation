#include "gpu/scene/CollisionPairBuffers.h"

#include <iostream>
#include <limits>

namespace {

constexpr std::uint32_t dispatch_component_count = 3;
constexpr std::uint32_t cloth_body_pair_component_count = 2;
constexpr std::uint32_t cloth_cloth_pair_component_count = 4;

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

void create_collision_pair_buffer(CollisionPairBuffer& buffers,
                                  std::uint32_t capacity,
                                  std::uint32_t component_count,
                                  QOpenGLFunctions_4_5_Core& gl)
{
    buffers.capacity = capacity;
    const auto pair_bytes = static_cast<GLsizeiptr>(static_cast<std::uint64_t>(capacity) *
                                                    sizeof(std::uint32_t) * component_count);
    create_buffer(buffers.pairs, pair_bytes, gl);
    create_buffer(buffers.pair_count, static_cast<GLsizeiptr>(sizeof(std::uint32_t)), gl);
    create_buffer(buffers.dispatch_size, static_cast<GLsizeiptr>(dispatch_component_count * sizeof(std::uint32_t)), gl);
    create_buffer(buffers.overflow_count, static_cast<GLsizeiptr>(sizeof(std::uint32_t)), gl);
}

void clear_normal_corrections(GLuint buffer, QOpenGLFunctions_4_5_Core& gl)
{
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(buffer,
                              GL_RGBA32I,
                              GL_RGBA_INTEGER,
                              GL_INT,
                              zero_int);
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
                                           std::uint32_t garment_count,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint64_t cloth_vertex_body_face_capacity_wide =
        static_cast<std::uint64_t>(vertex_count) * pair_capacity_multiplier;
    const std::uint64_t cloth_edge_body_edge_capacity_wide =
        static_cast<std::uint64_t>(edge_count) * pair_capacity_multiplier;
    const std::uint64_t cloth_face_body_vertex_capacity_wide =
        static_cast<std::uint64_t>(triangle_count) * pair_capacity_multiplier;
    std::uint32_t cloth_cloth_vertex_face_capacity = 0;
    if (cloth_vertex_body_face_capacity_wide > std::numeric_limits<std::uint32_t>::max() ||
        cloth_edge_body_edge_capacity_wide > std::numeric_limits<std::uint32_t>::max() ||
        cloth_face_body_vertex_capacity_wide > std::numeric_limits<std::uint32_t>::max() ||
        !calculate_cloth_cloth_pair_capacity(vertex_count, garment_count, cloth_cloth_vertex_face_capacity)) {
        std::cerr << "Collision pair capacity exceeds the supported 32-bit range.\n";
        release(gl);
        return false;
    }

    const auto cloth_vertex_body_face_capacity =
        static_cast<std::uint32_t>(cloth_vertex_body_face_capacity_wide);
    const auto cloth_edge_body_edge_capacity =
        static_cast<std::uint32_t>(cloth_edge_body_edge_capacity_wide);
    const auto cloth_face_body_vertex_capacity =
        static_cast<std::uint32_t>(cloth_face_body_vertex_capacity_wide);
    const bool has_sufficient_cloth_cloth_buffer =
        cloth_cloth_vertex_face_capacity == 0 ||
        (has_collision_pair_buffer(buffers_.cloth_cloth_vertex_face) &&
         buffers_.cloth_cloth_vertex_face.capacity >= cloth_cloth_vertex_face_capacity);

    if (has_collision_pair_buffer(buffers_.cloth_vertex_body_face) &&
        has_collision_pair_buffer(buffers_.cloth_edge_body_edge) &&
        has_collision_pair_buffer(buffers_.cloth_face_body_vertex) &&
        has_sufficient_cloth_cloth_buffer &&
        buffers_.normal_correction_sum_buffer != 0 &&
        buffers_.friction_correction_sum_buffer != 0 &&
        buffers_.vertex_capacity >= vertex_count &&
        buffers_.cloth_vertex_body_face.capacity >= cloth_vertex_body_face_capacity &&
        buffers_.cloth_edge_body_edge.capacity >= cloth_edge_body_edge_capacity &&
        buffers_.cloth_face_body_vertex.capacity >= cloth_face_body_vertex_capacity) {
        return true;
    }

    release(gl);

    buffers_.vertex_capacity = vertex_count;
    create_collision_pair_buffer(buffers_.cloth_vertex_body_face,
                                 cloth_vertex_body_face_capacity,
                                 cloth_body_pair_component_count,
                                 gl);
    create_collision_pair_buffer(buffers_.cloth_edge_body_edge,
                                 cloth_edge_body_edge_capacity,
                                 cloth_body_pair_component_count,
                                 gl);
    create_collision_pair_buffer(buffers_.cloth_face_body_vertex,
                                 cloth_face_body_vertex_capacity,
                                 cloth_body_pair_component_count,
                                 gl);
    if (cloth_cloth_vertex_face_capacity > 0) {
        create_collision_pair_buffer(buffers_.cloth_cloth_vertex_face,
                                     cloth_cloth_vertex_face_capacity,
                                     cloth_cloth_pair_component_count,
                                     gl);
    }
    create_buffer(buffers_.normal_correction_sum_buffer, static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u), gl);
    create_buffer(buffers_.friction_correction_sum_buffer, static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u), gl);

    const bool initialized =
        has_collision_pair_buffer(buffers_.cloth_vertex_body_face) &&
        has_collision_pair_buffer(buffers_.cloth_edge_body_edge) &&
        has_collision_pair_buffer(buffers_.cloth_face_body_vertex) &&
        (cloth_cloth_vertex_face_capacity == 0 ||
         has_collision_pair_buffer(buffers_.cloth_cloth_vertex_face)) &&
        buffers_.normal_correction_sum_buffer != 0 &&
        buffers_.friction_correction_sum_buffer != 0;
    if (!initialized) {
        release(gl);
    }
    return initialized;
}

bool CollisionPairBuffers::calculate_cloth_cloth_pair_capacity(std::uint32_t vertex_count,
                                                               std::uint32_t garment_count,
                                                               std::uint32_t& capacity)
{
    if (garment_count < 2u) {
        capacity = 0;
        return true;
    }

    const std::uint64_t directed_query_count =
        static_cast<std::uint64_t>(garment_count - 1u) * vertex_count;
    if (directed_query_count >
        std::numeric_limits<std::uint32_t>::max() / pair_capacity_multiplier) {
        capacity = 0;
        return false;
    }

    capacity = static_cast<std::uint32_t>(directed_query_count * pair_capacity_multiplier);
    return true;
}

void CollisionPairBufferView::clear_cloth_body_pair_counts(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_collision_pair_counts(cloth_vertex_body_face, gl);
    clear_collision_pair_counts(cloth_edge_body_edge, gl);
    clear_collision_pair_counts(cloth_face_body_vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CollisionPairBufferView::clear_cloth_cloth_pair_counts(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_collision_pair_counts(cloth_cloth_vertex_face, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CollisionPairBufferView::clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_normal_corrections(normal_correction_sum_buffer, gl);
    clear_normal_corrections(friction_correction_sum_buffer, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CollisionPairBufferView::clear_normal_correction_sums(QOpenGLFunctions_4_5_Core& gl) const
{
    clear_normal_corrections(normal_correction_sum_buffer, gl);
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
    delete_collision_pair_buffer(buffers_.cloth_cloth_vertex_face, gl);
    gl.glDeleteBuffers(1, &buffers_.normal_correction_sum_buffer);
    gl.glDeleteBuffers(1, &buffers_.friction_correction_sum_buffer);
    buffers_ = {};
}
