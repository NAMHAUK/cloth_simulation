#include "rendering/ViewerShader.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <glm/gtc/type_ptr.hpp>

namespace {
std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open shader file: " << path << '\n';
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}
}

bool ViewerShader::is_initialized() const
{
    return program_ != 0;
}

bool ViewerShader::load(const std::filesystem::path& vertex_shader_path,
                        const std::filesystem::path& fragment_shader_path,
                        QOpenGLFunctions_4_5_Core& gl)
{
    const auto vertex_shader_source = read_text_file(vertex_shader_path);
    const auto fragment_shader_source = read_text_file(fragment_shader_path);
    if (!vertex_shader_source || !fragment_shader_source) {
        return false;
    }

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source->c_str(), gl);
    if (vertex_shader == 0) {
        return false;
    }

    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source->c_str(), gl);
    if (fragment_shader == 0) {
        gl.glDeleteShader(vertex_shader);
        return false;
    }

    const GLuint next_program = gl.glCreateProgram();
    gl.glAttachShader(next_program, vertex_shader);
    gl.glAttachShader(next_program, fragment_shader);
    gl.glLinkProgram(next_program);

    GLint success = 0;
    gl.glGetProgramiv(next_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(next_program, sizeof(log), nullptr, log);
        std::cerr << "Program link failed: " << log << '\n';
        gl.glDeleteShader(vertex_shader);
        gl.glDeleteShader(fragment_shader);
        gl.glDeleteProgram(next_program);
        return false;
    }

    gl.glDeleteShader(vertex_shader);
    gl.glDeleteShader(fragment_shader);

    release(gl);
    program_ = next_program;
    mvp_location_ = gl.glGetUniformLocation(program_, "uMVP");
    solid_mode_location_ = gl.glGetUniformLocation(program_, "uUseSolidColor");
    solid_color_location_ = gl.glGetUniformLocation(program_, "uSolidColor");
    animation_mode_location_ = gl.glGetUniformLocation(program_, "uUseAnimationBuffer");
    animation_frame_index_location_ = gl.glGetUniformLocation(program_, "uAnimationFrameIndex");
    animation_vertex_count_location_ = gl.glGetUniformLocation(program_, "uAnimationVertexCount");
    return true;
}

void ViewerShader::bind(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);
}

void ViewerShader::set_mvp(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || mvp_location_ < 0) {
        return;
    }

    gl.glProgramUniformMatrix4fv(program_, mvp_location_, 1, GL_FALSE, glm::value_ptr(mvp));
}

void ViewerShader::set_attribute_position_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || animation_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, animation_mode_location_, 0);
}

void ViewerShader::set_character_animation_mode(std::uint32_t frame_index,
                                                std::uint32_t vertex_count,
                                                QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }
    if (animation_mode_location_ >= 0) {
        gl.glProgramUniform1i(program_, animation_mode_location_, 1);
    }
    if (animation_frame_index_location_ >= 0) {
        gl.glProgramUniform1ui(program_, animation_frame_index_location_, frame_index);
    }
    if (animation_vertex_count_location_ >= 0) {
        gl.glProgramUniform1ui(program_, animation_vertex_count_location_, vertex_count);
    }
}

void ViewerShader::set_solid_color(const glm::vec3& color, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }
    if (solid_mode_location_ >= 0) {
        gl.glProgramUniform1i(program_, solid_mode_location_, 1);
    }
    if (solid_color_location_ >= 0) {
        gl.glProgramUniform3f(program_, solid_color_location_, color.r, color.g, color.b);
    }
}

void ViewerShader::set_vertex_color_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || solid_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, solid_mode_location_, 0);
}

void ViewerShader::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    mvp_location_ = -1;
    solid_mode_location_ = -1;
    solid_color_location_ = -1;
    animation_mode_location_ = -1;
    animation_frame_index_location_ = -1;
    animation_vertex_count_location_ = -1;
}

GLuint ViewerShader::compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint shader = gl.glCreateShader(type);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile failed: " << log << '\n';
        gl.glDeleteShader(shader);
        return 0;
    }

    return shader;
}
