#pragma once

#include <QOpenGLFunctions_4_5_Core>

struct CharacterRenderBuffers;
struct PbdCharacterBufferSet;
struct ClothRenderBuffers;
struct PbdClothBufferSet;
struct CollisionBuffers;

class PbdBufferBindings final
{
public:
    PbdBufferBindings() = default;
    PbdBufferBindings(const PbdBufferBindings&) = delete;
    PbdBufferBindings& operator=(const PbdBufferBindings&) = delete;

    void initialize(QOpenGLFunctions_4_5_Core& gl);
    void bind_character(const CharacterRenderBuffers& buffers,
                        const PbdCharacterBufferSet& pbd_buffers,
                        QOpenGLFunctions_4_5_Core& gl) const;
    void bind_cloth(const ClothRenderBuffers& buffers,
                    const PbdClothBufferSet& pbd_buffers,
                    QOpenGLFunctions_4_5_Core& gl) const;
    void bind_collision(const CollisionBuffers& buffers, QOpenGLFunctions_4_5_Core& gl) const;
    void reset_character_bindings(QOpenGLFunctions_4_5_Core& gl) const;
    void reset_cloth_bindings(QOpenGLFunctions_4_5_Core& gl) const;
    void reset_collision_bindings(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint dummy_buffer_ = 0;
};
