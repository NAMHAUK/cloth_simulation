#include "rendering/BackgroundGradient.h"

#include "utils/FileUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>
#include <stdexcept>

bool BackgroundGradient::is_initialized() const
{
    return program_ != 0 && vao_ != 0;
}

void BackgroundGradient::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const auto vertex_shader_path = shader_dir / "rendering" / "background.vert";
    const auto fragment_shader_path = shader_dir / "rendering" / "background.frag";
    program_ = load_program(vertex_shader_path, fragment_shader_path, gl);

    const GLint top_color_location = require_uniform_location(program_, "uTopColor", gl);
    const GLint bottom_color_location = require_uniform_location(program_, "uBottomColor", gl);
    gl.glProgramUniform3f(program_, top_color_location, top_color_.r, top_color_.g, top_color_.b);
    gl.glProgramUniform3f(program_, bottom_color_location, bottom_color_.r, bottom_color_.g, bottom_color_.b);

    gl.glCreateVertexArrays(1, &vao_);
}

void BackgroundGradient::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
}

void BackgroundGradient::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteVertexArrays(1, &vao_);
    gl.glDeleteProgram(program_);

    program_ = 0;
    vao_ = 0;
}

GLuint BackgroundGradient::load_program(const std::filesystem::path& vertex_shader_path,
                                        const std::filesystem::path& fragment_shader_path,
                                        QOpenGLFunctions_4_5_Core& gl) const
{
    const auto vertex_shader_source = read_text_file(vertex_shader_path);
    const auto fragment_shader_source = read_text_file(fragment_shader_path);
    if (!vertex_shader_source || !fragment_shader_source) {
        throw std::runtime_error("Failed to load background shader source.");
    }

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source->c_str(), gl);
    if (vertex_shader == 0) {
        throw std::runtime_error("Failed to compile background vertex shader.");
    }

    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source->c_str(), gl);
    if (fragment_shader == 0) {
        gl.glDeleteShader(vertex_shader);
        throw std::runtime_error("Failed to compile background fragment shader.");
    }

    const GLuint next_program = gl.glCreateProgram();
    gl.glAttachShader(next_program, vertex_shader);
    gl.glAttachShader(next_program, fragment_shader);
    gl.glLinkProgram(next_program);

    GLint linked = 0;
    gl.glGetProgramiv(next_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(next_program, sizeof(log), nullptr, log);
        gl.glDeleteShader(vertex_shader);
        gl.glDeleteShader(fragment_shader);
        gl.glDeleteProgram(next_program);
        throw std::runtime_error(std::string("Background program link failed: ") + log);
    }

    gl.glDeleteShader(vertex_shader);
    gl.glDeleteShader(fragment_shader);
    return next_program;
}

GLuint BackgroundGradient::compile_shader(GLenum type,
                                          const char* source,
                                          QOpenGLFunctions_4_5_Core& gl) const
{
    const GLuint shader = gl.glCreateShader(type);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint compiled = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Background shader compile failed: " << log << '\n';
        gl.glDeleteShader(shader);
        return 0;
    }

    return shader;
}
