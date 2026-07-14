#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CollisionPairBuffer final {
    GLuint pairs = 0;
    GLuint pair_count = 0;
    GLuint dispatch_size = 0;
    GLuint overflow_count = 0;
    std::uint32_t capacity = 0;
};

struct CollisionPairBufferView final {
    CollisionPairBuffer cloth_vertex_body_face;
    CollisionPairBuffer cloth_edge_body_edge;
    CollisionPairBuffer cloth_face_body_vertex;
    CollisionPairBuffer cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    std::uint32_t vertex_capacity = 0;

    void clear_cloth_body_pair_counts(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_cloth_cloth_pair_counts(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_normal_correction_sums(QOpenGLFunctions_4_5_Core& gl) const;
};

class CollisionPairBuffers final {
public:
    static constexpr std::uint32_t pair_capacity_multiplier = 8;

    CollisionPairBuffers() = default;
    CollisionPairBuffers(const CollisionPairBuffers&) = delete;
    CollisionPairBuffers& operator=(const CollisionPairBuffers&) = delete;

    bool ensure_capacity(std::uint32_t vertex_count,
                         std::uint32_t triangle_count,
                         std::uint32_t edge_count,
                         std::uint32_t garment_count,
                         QOpenGLFunctions_4_5_Core& gl);
    static bool calculate_cloth_cloth_pair_capacity(std::uint32_t vertex_count,
                                                    std::uint32_t garment_count,
                                                    std::uint32_t& capacity);
    CollisionPairBufferView view() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    CollisionPairBufferView buffers_;
};
