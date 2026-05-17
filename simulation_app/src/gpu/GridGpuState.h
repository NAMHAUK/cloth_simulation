#pragma once

#include <QOpenGLFunctions_4_5_Core>

class GridGpuState final {
public:
    GridGpuState() = default;
    GridGpuState(const GridGpuState&) = delete;
    GridGpuState& operator=(const GridGpuState&) = delete;

    bool initialized() const;

    void upload(QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint vao_ = 0;
    GLuint vertex_buffer_ = 0;
    GLsizei vertex_count_ = 0;
};
