#include "rendering/SceneRenderShader.h"

#include "utils/ShaderUtils.h"

#include <glm/gtc/type_ptr.hpp>

bool SceneRenderShader::is_initialized() const
{
    return program_ != 0;
}

void SceneRenderShader::load(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path vertex_shader_path = shader_dir / "rendering" / "viewer.vert";
    const std::filesystem::path fragment_shader_path = shader_dir / "rendering" / "viewer.frag";
    program_ = load_render_program(vertex_shader_path, fragment_shader_path, gl);

    mvp_loc_ = gl.glGetUniformLocation(program_, "uMVP");
    solid_mode_loc_ = gl.glGetUniformLocation(program_, "uUseSolidColor");
    solid_color_loc_ = gl.glGetUniformLocation(program_, "uSolidColor");
    opacity_loc_ = gl.glGetUniformLocation(program_, "uOpacity");
    position_buffer_mode_loc_ = gl.glGetUniformLocation(program_, "uUsePositionBuffer");
    normal_lighting_mode_loc_ = gl.glGetUniformLocation(program_, "uUseNormalLighting");
    light_direction_loc_ = gl.glGetUniformLocation(program_, "uLightDirectionWorld");
    fill_light_direction_loc_ = gl.glGetUniformLocation(program_, "uFillLightDirectionWorld");
    ambient_strength_loc_ = gl.glGetUniformLocation(program_, "uAmbientStrength");
    diffuse_strength_loc_ = gl.glGetUniformLocation(program_, "uDiffuseStrength");
    fill_diffuse_strength_loc_ = gl.glGetUniformLocation(program_, "uFillDiffuseStrength");
}

void SceneRenderShader::bind(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);
}

void SceneRenderShader::set_mvp(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || mvp_loc_ < 0) {
        return;
    }

    gl.glProgramUniformMatrix4fv(program_, mvp_loc_, 1, GL_FALSE, glm::value_ptr(mvp));
}

void SceneRenderShader::set_attribute_position_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || position_buffer_mode_loc_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, position_buffer_mode_loc_, 0);
}

void SceneRenderShader::set_character_position_buffer_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || position_buffer_mode_loc_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, position_buffer_mode_loc_, 1);
}

void SceneRenderShader::set_solid_color(const glm::vec3& color, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }
    if (solid_mode_loc_ >= 0) {
        gl.glProgramUniform1i(program_, solid_mode_loc_, 1);
    }
    if (solid_color_loc_ >= 0) {
        gl.glProgramUniform3f(program_, solid_color_loc_, color.r, color.g, color.b);
    }
}

void SceneRenderShader::set_vertex_color_mode(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || solid_mode_loc_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, solid_mode_loc_, 0);
}

void SceneRenderShader::set_opacity(float opacity, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || opacity_loc_ < 0) {
        return;
    }

    gl.glProgramUniform1f(program_, opacity_loc_, opacity);
}

void SceneRenderShader::set_lighting(const glm::vec3& light_direction_world,
                                     const glm::vec3& fill_light_direction_world,
                                     float ambient_strength,
                                     float diffuse_strength,
                                     float fill_diffuse_strength,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }
    if (light_direction_loc_ >= 0) {
        gl.glProgramUniform3f(program_,
                              light_direction_loc_,
                              light_direction_world.x,
                              light_direction_world.y,
                              light_direction_world.z);
    }
    if (fill_light_direction_loc_ >= 0) {
        gl.glProgramUniform3f(program_,
                              fill_light_direction_loc_,
                              fill_light_direction_world.x,
                              fill_light_direction_world.y,
                              fill_light_direction_world.z);
    }
    if (ambient_strength_loc_ >= 0) {
        gl.glProgramUniform1f(program_, ambient_strength_loc_, ambient_strength);
    }
    if (diffuse_strength_loc_ >= 0) {
        gl.glProgramUniform1f(program_, diffuse_strength_loc_, diffuse_strength);
    }
    if (fill_diffuse_strength_loc_ >= 0) {
        gl.glProgramUniform1f(program_, fill_diffuse_strength_loc_, fill_diffuse_strength);
    }
}

void SceneRenderShader::set_normal_lighting_enabled(bool enabled, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || normal_lighting_mode_loc_ < 0) {
        return;
    }

    gl.glProgramUniform1i(program_, normal_lighting_mode_loc_, enabled ? 1 : 0);
}

void SceneRenderShader::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    mvp_loc_ = -1;
    solid_mode_loc_ = -1;
    solid_color_loc_ = -1;
    opacity_loc_ = -1;
    position_buffer_mode_loc_ = -1;
    normal_lighting_mode_loc_ = -1;
    light_direction_loc_ = -1;
    fill_light_direction_loc_ = -1;
    ambient_strength_loc_ = -1;
    diffuse_strength_loc_ = -1;
    fill_diffuse_strength_loc_ = -1;
}
