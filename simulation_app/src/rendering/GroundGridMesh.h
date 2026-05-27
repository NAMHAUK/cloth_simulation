#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include <glm/vec3.hpp>

class GroundGridMesh final {
public:
    GroundGridMesh() = default;
    GroundGridMesh(const GroundGridMesh&) = delete;
    GroundGridMesh& operator=(const GroundGridMesh&) = delete;

    bool initialized() const;
    const glm::vec3& color() const;

    void upload(QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    glm::vec3 color_{0.62f, 0.64f, 0.68f};
    GLuint vao_ = 0;
    GLuint vertex_buffer_ = 0;
    GLsizei vertex_count_ = 0;
};
