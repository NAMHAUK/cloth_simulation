#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CollisionWorkspaceBufferView final {
    GLuint pair_record_buffer = 0;
    GLuint pair_count_buffer = 0;
    GLuint edge_pair_record_buffer = 0;
    GLuint edge_pair_count_buffer = 0;
    GLuint correction_sum_buffer = 0;
    GLuint contact_candidate_head_buffer = 0;
    GLuint contact_candidate_count_buffer = 0;
    GLuint contact_candidate_buffer = 0;
    GLuint contact_candidate_next_buffer = 0;
    std::uint32_t vertex_capacity = 0;
    std::uint32_t pair_capacity = 0;
    std::uint32_t edge_pair_capacity = 0;
    std::uint32_t contact_candidate_capacity = 0;

    void clear(QOpenGLFunctions_4_5_Core& gl) const;
};

class CollisionWorkspaceBuffers final {
public:
    static constexpr std::uint32_t pair_capacity_multiplier = 4;

    CollisionWorkspaceBuffers() = default;
    CollisionWorkspaceBuffers(const CollisionWorkspaceBuffers&) = delete;
    CollisionWorkspaceBuffers& operator=(const CollisionWorkspaceBuffers&) = delete;

    bool ensure_capacity(std::uint32_t vertex_count,
                          std::uint32_t triangle_count,
                          std::uint32_t edge_count,
                          std::uint32_t max_contacts_per_vertex,
                          QOpenGLFunctions_4_5_Core& gl);
    CollisionWorkspaceBufferView view() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    CollisionWorkspaceBufferView buffers_;
};
