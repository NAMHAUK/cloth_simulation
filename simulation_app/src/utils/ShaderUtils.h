#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <QOpenGLFunctions_4_5_Core>

std::string load_shader_source(const std::filesystem::path& shader_path);

GLuint load_compute_program(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
GLuint load_render_program(const std::filesystem::path& vertex_shader_path,
                           const std::filesystem::path& fragment_shader_path,
                           QOpenGLFunctions_4_5_Core& gl);

GLint require_uniform_location(GLuint program, const char* name, QOpenGLFunctions_4_5_Core& gl);

constexpr std::uint32_t compute_group_count(std::uint32_t item_count, std::uint32_t local_size)
{
    return (item_count + local_size - 1u) / local_size;
}
