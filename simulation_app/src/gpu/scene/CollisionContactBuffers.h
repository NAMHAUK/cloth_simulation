#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct ContactPairBuffers final {
    GLuint pairs = 0;
    GLuint pair_count = 0;
    GLuint dispatch_size = 0;
    GLuint overflow_count = 0;
    std::uint32_t capacity = 0;
};

struct CollisionContactBufferView final {
    ContactPairBuffers cloth_vertex_body_face;
    ContactPairBuffers cloth_edge_body_edge;
    ContactPairBuffers cloth_face_body_vertex;
    GLuint correction_sum_buffer = 0;
    std::uint32_t vertex_capacity = 0;

    void clear_contact_counts(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const;
};

class CollisionContactBuffers final {
public:
    static constexpr std::uint32_t pair_capacity_multiplier = 4;

    CollisionContactBuffers() = default;
    CollisionContactBuffers(const CollisionContactBuffers&) = delete;
    CollisionContactBuffers& operator=(const CollisionContactBuffers&) = delete;

    bool ensure_capacity(std::uint32_t vertex_count,
                         std::uint32_t triangle_count,
                         std::uint32_t edge_count,
                         QOpenGLFunctions_4_5_Core& gl);
    CollisionContactBufferView view() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    CollisionContactBufferView buffers_;
};
