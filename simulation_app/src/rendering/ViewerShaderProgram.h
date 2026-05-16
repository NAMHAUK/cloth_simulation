#pragma once

#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QOpenGLFunctions_4_5_Core>

class ViewerShaderProgram final {
public:
    ViewerShaderProgram() = default;
    ViewerShaderProgram(const ViewerShaderProgram&) = delete;
    ViewerShaderProgram& operator=(const ViewerShaderProgram&) = delete;

    bool initialized() const;

    bool load(const std::filesystem::path& vertex_shader_path,
              const std::filesystem::path& fragment_shader_path,
              QOpenGLFunctions_4_5_Core& gl);
    void bind(QOpenGLFunctions_4_5_Core& gl) const;
    void set_mvp(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) const;
    void set_solid_color(const glm::vec3& color, QOpenGLFunctions_4_5_Core& gl) const;
    void set_vertex_color_mode(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl);

    GLuint program_ = 0;
    GLint mvp_location_ = -1;
    GLint solid_mode_location_ = -1;
    GLint solid_color_location_ = -1;
};
