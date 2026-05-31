#pragma once

#include "utils/FileUtils.h"

#include <cstdint>
#include <filesystem>
#include <iostream>

#include <QOpenGLFunctions_4_5_Core>

inline GLuint compile_compute_shader(const char* source, const char* error_context, QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint shader = gl.glCreateShader(GL_COMPUTE_SHADER);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << error_context << " compute shader compile failed: " << log << '\n';
        gl.glDeleteShader(shader);
        return 0;
    }

    return shader;
}

inline GLuint load_compute_program(const std::filesystem::path& shader_path,
                                   const char* error_context,
                                   QOpenGLFunctions_4_5_Core& gl)
{
    const auto shader_source = read_text_file(shader_path);
    if (!shader_source) {
        return 0;
    }

    const GLuint shader = compile_compute_shader(shader_source->c_str(), error_context, gl);
    if (shader == 0) {
        return 0;
    }

    const GLuint program = gl.glCreateProgram();
    gl.glAttachShader(program, shader);
    gl.glLinkProgram(program);

    GLint success = 0;
    gl.glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << error_context << " compute program link failed: " << log << '\n';
        gl.glDeleteShader(shader);
        gl.glDeleteProgram(program);
        return 0;
    }

    gl.glDeleteShader(shader);
    return program;
}

constexpr std::uint32_t compute_group_count(std::uint32_t item_count, std::uint32_t local_size)
{
    return (item_count + local_size - 1u) / local_size;
}
