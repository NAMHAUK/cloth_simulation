#pragma once

#include <QOpenGLFunctions_4_5_Core>

#include <filesystem>

#include <glm/vec3.hpp>

class BackgroundGradient final
{
public:
    BackgroundGradient() = default;
    BackgroundGradient(const BackgroundGradient&) = delete;
    BackgroundGradient& operator=(const BackgroundGradient&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint load_program(const std::filesystem::path& vertex_shader_path,
                        const std::filesystem::path& fragment_shader_path,
                        QOpenGLFunctions_4_5_Core& gl) const;
    GLuint compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl) const;

    glm::vec3 top_color_{0.58f, 0.59f, 0.62f};
    glm::vec3 bottom_color_{0.94f, 0.95f, 0.97f};
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vertex_buffer_ = 0;
    GLint top_color_loc_ = -1;
    GLint bottom_color_loc_ = -1;
};
