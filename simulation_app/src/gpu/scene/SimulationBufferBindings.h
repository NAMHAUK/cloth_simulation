#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterBufferSet;
struct ClothBufferSet;

struct CollisionCandidateBuffers final
{
    GLuint candidate_buffer = 0;
    GLuint count_buffer = 0;
    GLuint dispatch_size_buffer = 0;
    std::uint32_t max_pairs = 0;
};

struct CollisionBuffers final
{
    CollisionCandidateBuffers cloth_vertex_body_face;
    CollisionCandidateBuffers cloth_edge_body_edge;
    CollisionCandidateBuffers cloth_face_body_vertex;
    CollisionCandidateBuffers cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    GLuint contact_motion_delta_sum_buffer = 0;
};

class SimulationBufferBindings final
{
public:
    SimulationBufferBindings() = default;
    SimulationBufferBindings(const SimulationBufferBindings&) = delete;
    SimulationBufferBindings& operator=(const SimulationBufferBindings&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void bind_character(const CharacterBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_cloth(const ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_collision(const CollisionBuffers& buffers, QOpenGLFunctions_4_5_Core& gl) const;

    void reset_character_bindings(QOpenGLFunctions_4_5_Core& gl) const;
    void reset_cloth_bindings(QOpenGLFunctions_4_5_Core& gl) const;
    void reset_collision_bindings(QOpenGLFunctions_4_5_Core& gl) const;

private:
    GLuint dummy_buffer_ = 0;
};
