#include "rendering/BackgroundGradient.h"

#include "support/FileUtils.h"

#include <array>
#include <filesystem>
#include <iostream>

namespace {
const std::filesystem::path vertex_shader_path =
    std::filesystem::path(PROJECT_ROOT_DIR) / "simulation_app" / "shaders" / "background.vert";

const std::filesystem::path fragment_shader_path =
    std::filesystem::path(PROJECT_ROOT_DIR) / "simulation_app" / "shaders" / "background.frag";
}

bool BackgroundGradient::is_initialized() const
{
    return program_ != 0 && vao_ != 0 && vertex_buffer_ != 0;
}

bool BackgroundGradient::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    if (is_initialized()) {
        return true;
    }

    const GLuint next_program = load_program(gl);
    if (next_program == 0) {
        return false;
    }

    constexpr std::array<float, 12> vertices{
        -1.0f, -1.0f, 0.0f, 0.0f,
         3.0f, -1.0f, 2.0f, 0.0f,
        -1.0f,  3.0f, 0.0f, 2.0f,
    };

    release(gl);
    program_ = next_program;
    top_color_location_ = gl.glGetUniformLocation(program_, "uTopColor");
    bottom_color_location_ = gl.glGetUniformLocation(program_, "uBottomColor");

    if (top_color_location_ >= 0) {
        gl.glProgramUniform3f(program_, top_color_location_, top_color_.r, top_color_.g, top_color_.b);
    }
    if (bottom_color_location_ >= 0) {
        gl.glProgramUniform3f(program_, bottom_color_location_, bottom_color_.r, bottom_color_.g, bottom_color_.b);
    }

    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &vertex_buffer_);
    gl.glNamedBufferData(
        vertex_buffer_,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    constexpr GLsizei stride = 4 * static_cast<GLsizei>(sizeof(float));
    constexpr GLuint vertex_binding_index = 0;
    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint uv_attribute_location = 1;
    constexpr GLuint position_relative_offset = 0;
    constexpr GLuint uv_relative_offset = 2 * sizeof(float);

    gl.glVertexArrayVertexBuffer(vao_, vertex_binding_index, vertex_buffer_, 0, stride);
    gl.glEnableVertexArrayAttrib(vao_, position_attribute_location);
    gl.glVertexArrayAttribFormat(
        vao_,
        position_attribute_location,
        2,
        GL_FLOAT,
        GL_FALSE,
        position_relative_offset
    );
    gl.glVertexArrayAttribBinding(vao_, position_attribute_location, vertex_binding_index);
    gl.glEnableVertexArrayAttrib(vao_, uv_attribute_location);
    gl.glVertexArrayAttribFormat(
        vao_,
        uv_attribute_location,
        2,
        GL_FLOAT,
        GL_FALSE,
        uv_relative_offset
    );
    gl.glVertexArrayAttribBinding(vao_, uv_attribute_location, vertex_binding_index);
    return true;
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
    if (vertex_buffer_ != 0) {
        gl.glDeleteBuffers(1, &vertex_buffer_);
    }
    if (vao_ != 0) {
        gl.glDeleteVertexArrays(1, &vao_);
    }
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    vao_ = 0;
    vertex_buffer_ = 0;
    top_color_location_ = -1;
    bottom_color_location_ = -1;
}

GLuint BackgroundGradient::load_program(QOpenGLFunctions_4_5_Core& gl) const
{
    const auto vertex_shader_source = read_text_file(vertex_shader_path);
    const auto fragment_shader_source = read_text_file(fragment_shader_path);
    if (!vertex_shader_source || !fragment_shader_source) {
        return 0;
    }

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source->c_str(), gl);
    if (vertex_shader == 0) {
        return 0;
    }

    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source->c_str(), gl);
    if (fragment_shader == 0) {
        gl.glDeleteShader(vertex_shader);
        return 0;
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
        std::cerr << "Background program link failed: " << log << '\n';
        gl.glDeleteShader(vertex_shader);
        gl.glDeleteShader(fragment_shader);
        gl.glDeleteProgram(next_program);
        return 0;
    }

    gl.glDeleteShader(vertex_shader);
    gl.glDeleteShader(fragment_shader);
    return next_program;
}

GLuint BackgroundGradient::compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl) const
{
    const GLuint shader = gl.glCreateShader(type);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Background shader compile failed: " << log << '\n';
        gl.glDeleteShader(shader);
        return 0;
    }

    return shader;
}
