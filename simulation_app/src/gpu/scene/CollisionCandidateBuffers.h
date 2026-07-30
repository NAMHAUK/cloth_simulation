#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CollisionCandidateBuffer final {
    GLuint candidates = 0;
    GLuint candidate_count = 0;
    GLuint dispatch_size = 0;
    GLuint overflow_count = 0;
    std::uint32_t capacity = 0;
};

struct CollisionCandidateBufferView final {
    CollisionCandidateBuffer cloth_vertex_body_face;
    CollisionCandidateBuffer cloth_edge_body_edge;
    CollisionCandidateBuffer cloth_face_body_vertex;
    CollisionCandidateBuffer cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    GLuint contact_motion_delta_sum_buffer = 0;
    std::uint32_t vertex_capacity = 0;

    void clear_cloth_body_candidate_counts(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_cloth_cloth_candidate_counts(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_correction_sums(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_normal_correction_sums(QOpenGLFunctions_4_5_Core& gl) const;
};

class CollisionCandidateBuffers final {
public:
    static constexpr std::uint32_t candidate_capacity_multiplier = 8;

    CollisionCandidateBuffers() = default;
    CollisionCandidateBuffers(const CollisionCandidateBuffers&) = delete;
    CollisionCandidateBuffers& operator=(const CollisionCandidateBuffers&) = delete;

    bool ensure_capacity(std::uint32_t vertex_count,
                         std::uint32_t triangle_count,
                         std::uint32_t edge_count,
                         std::uint32_t garment_count,
                         QOpenGLFunctions_4_5_Core& gl);
    static bool calculate_cloth_cloth_candidate_capacity(std::uint32_t vertex_count,
                                                    std::uint32_t garment_count,
                                                    std::uint32_t& capacity);
    CollisionCandidateBufferView view() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    CollisionCandidateBufferView buffers_;
};
