#pragma once

#include "utils/FileUtils.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

namespace {

inline GLuint compile_compute_shader(const char* source,
                                     const std::filesystem::path& shader_path,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint shader = gl.glCreateShader(GL_COMPUTE_SHADER);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        gl.glDeleteShader(shader);
        throw std::runtime_error("Failed to compile compute shader: " + shader_path.string() + "\n" + log);
    }

    return shader;
}

inline bool parse_shader_include(const std::string& line, std::filesystem::path& include_path)
{
    const std::size_t directive_start = line.find_first_not_of(" \t");
    if (directive_start == std::string::npos || line.compare(directive_start, 8u, "#include") != 0) {
        return false;
    }

    const std::size_t quote_start = line.find('"', directive_start + 8u);
    if (quote_start == std::string::npos) {
        return false;
    }

    const std::size_t quote_end = line.find('"', quote_start + 1u);
    if (quote_end == std::string::npos) {
        return false;
    }

    include_path = line.substr(quote_start + 1u, quote_end - quote_start - 1u);
    return true;
}

inline std::optional<std::string> load_shader_source(const std::filesystem::path& shader_path,
                                                     std::vector<std::filesystem::path> include_stack)
{
    const std::filesystem::path normalized_path = std::filesystem::absolute(shader_path).lexically_normal();
    if (std::find(include_stack.begin(), include_stack.end(), normalized_path) != include_stack.end()) {
        std::cerr << "Shader include cycle detected: " << normalized_path << '\n';
        return std::nullopt;
    }

    const auto source = read_text_file(normalized_path);
    if (!source) {
        return std::nullopt;
    }

    include_stack.push_back(normalized_path);

    std::istringstream input(*source);
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        std::filesystem::path include_path;
        if (parse_shader_include(line, include_path)) {
            const auto include_source =
                load_shader_source(normalized_path.parent_path() / include_path, include_stack);
            if (!include_source) {
                return std::nullopt;
            }
            output << *include_source;
            if (include_source->empty() || include_source->back() != '\n') {
                output << '\n';
            }
            continue;
        }

        output << line << '\n';
    }

    return output.str();
}

}

inline GLuint load_compute_program(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    const auto shader_source = load_shader_source(shader_path, {});
    if (!shader_source) {
        throw std::runtime_error("Failed to load compute shader source: " + shader_path.string());
    }

    const GLuint shader = compile_compute_shader(shader_source->c_str(), shader_path, gl);
    const GLuint program = gl.glCreateProgram();
    gl.glAttachShader(program, shader);
    gl.glLinkProgram(program);

    GLint linked = 0;
    gl.glGetProgramiv(program, GL_LINK_STATUS, &linked);
    gl.glDeleteShader(shader);
    if (!linked) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        gl.glDeleteProgram(program);
        throw std::runtime_error("Failed to link compute program: " + shader_path.string() + "\n" + log);
    }

    return program;
}

inline GLint require_uniform_location(GLuint program, const char* name, QOpenGLFunctions_4_5_Core& gl)
{
    const GLint location = gl.glGetUniformLocation(program, name);
    if (location < 0) {
        throw std::runtime_error(std::string{"Missing required uniform: "} + name);
    }
    return location;
}

constexpr std::uint32_t compute_group_count(std::uint32_t item_count, std::uint32_t local_size)
{
    return (item_count + local_size - 1u) / local_size;
}
