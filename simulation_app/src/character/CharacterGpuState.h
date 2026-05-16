#pragma once

#include "assets/MotionAsset.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuState final {
public:
    CharacterGpuState() = default;
    CharacterGpuState(const CharacterGpuState&) = delete;
    CharacterGpuState& operator=(const CharacterGpuState&) = delete;

    bool initialized() const;

    void upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl);
    void upload_frame(const CharacterMesh& character_mesh, std::uint32_t frame_index, QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint vao_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint index_buffer_ = 0;
    GLsizei index_count_ = 0;
};
