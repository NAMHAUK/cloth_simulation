#pragma once

#include <QOpenGLFunctions_4_5_Core>

struct CharacterBufferSet;
struct ClothBufferSet;
struct CollisionBuffers;

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

    void restore_character(QOpenGLFunctions_4_5_Core& gl) const;
    void restore_cloth(QOpenGLFunctions_4_5_Core& gl) const;
    void restore_collision(QOpenGLFunctions_4_5_Core& gl) const;

private:
    GLuint fallback_buffer_ = 0;
};
