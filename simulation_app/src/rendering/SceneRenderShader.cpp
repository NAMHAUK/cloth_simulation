#include "rendering/SceneRenderShader.h"

#include "utils/FileUtils.h"

#include <iostream>

#include <glm/gtc/type_ptr.hpp>

bool SceneRenderShader::is_initialized() const
{
    return program_ != 0;
}

bool SceneRenderShader::load(const std::filesystem::path& vertex_shader_path,
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

    program_ = gl.glCreateProgram();
    gl.glAttachShader(program_, vertex_shader);
    gl.glAttachShader(program_, fragment_shader);
    gl.glLinkProgram(program_);

    GLint success = 0;
    gl.glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "Program link failed: " << log << '\n';
        gl.glDeleteShader(vertex_shader);
        gl.glDeleteShader(fragment_shader);
        gl.glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    gl.glDeleteShader(vertex_shader);
    gl.glDeleteShader(fragment_shader);

    mvp_location_ = gl.glGetUniformLocation(program_, "uMVP");
    solid_mode_location_ = gl.glGetUniformLocation(program_, "uUseSolidColor");
    solid_color_location_ = gl.glGetUniformLocation(program_, "uSolidColor");
    position_buffer_mode_location_ = gl.glGetUniformLocation(program_, "uUsePositionBuffer");
    normal_lighting_mode_location_ = gl.glGetUniformLocation(program_, "uUseNormalLighting");
    light_direction_location_ = gl.glGetUniformLocation(program_, "uLightDirectionWorld");
    ambient_strength_location_ = gl.glGetUniformLocation(program_, "uAmbientStrength");
    diffuse_strength_location_ = gl.glGetUniformLocation(program_, "uDiffuseStrength");
    return true;
}

void SceneRenderShader::bind(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);
}

void SceneRenderShader::set_mvp(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || mvp_location_ < 0) {
        return;
    }

    gl.glProgramUniformMatrix4fv(program_, mvp_location_, 1, GL_FALSE, glm::value_ptr(mvp));
}

void SceneRenderShader::set_attribute_position_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || position_buffer_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, position_buffer_mode_location_, 0);
}

void SceneRenderShader::set_character_position_buffer_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || position_buffer_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, position_buffer_mode_location_, 1);
}

void SceneRenderShader::set_solid_color(const glm::vec3& color, QOpenGLFunctions_4_5_Core& gl) const
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

void SceneRenderShader::set_vertex_color_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || solid_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, solid_mode_location_, 0);
}

void SceneRenderShader::set_lighting(const glm::vec3& light_direction_world,
                                     float ambient_strength,
                                     float diffuse_strength,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }
    if (light_direction_location_ >= 0) {
        gl.glProgramUniform3f(
            program_,
            light_direction_location_,
            light_direction_world.x,
            light_direction_world.y,
            light_direction_world.z
        );
    }
    if (ambient_strength_location_ >= 0) {
        gl.glProgramUniform1f(program_, ambient_strength_location_, ambient_strength);
    }
    if (diffuse_strength_location_ >= 0) {
        gl.glProgramUniform1f(program_, diffuse_strength_location_, diffuse_strength);
    }
}

void SceneRenderShader::set_normal_lighting_enabled(bool enabled, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || normal_lighting_mode_location_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, normal_lighting_mode_location_, enabled ? 1 : 0);
}

void SceneRenderShader::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    mvp_location_ = -1;
    solid_mode_location_ = -1;
    solid_color_location_ = -1;
    position_buffer_mode_location_ = -1;
    normal_lighting_mode_location_ = -1;
    light_direction_location_ = -1;
    ambient_strength_location_ = -1;
    diffuse_strength_location_ = -1;
}

GLuint SceneRenderShader::compile_shader(GLenum type, const char* source, QOpenGLFunctions_4_5_Core& gl)
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
