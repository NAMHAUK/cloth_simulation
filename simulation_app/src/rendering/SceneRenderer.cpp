#include "rendering/SceneRenderer.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"
#include "utils/ShaderUtils.h"

#include <cassert>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
const glm::vec3 light_direction_world = glm::normalize(glm::vec3{-0.4f, 0.8f, 0.3f});
const glm::vec3 fill_light_direction_world = glm::normalize(glm::vec3{0.3f, 0.6f, -0.8f});
constexpr float ambient_strength = 0.35f;
constexpr float diffuse_strength = 0.65f;
constexpr float fill_diffuse_strength = 0.35f;
}

bool SceneRenderer::is_initialized() const
{
    return program_ != 0;
}

void SceneRenderer::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const auto vertex_shader_path = shader_dir / "rendering" / "viewer.vert";
    const auto fragment_shader_path = shader_dir / "rendering" / "viewer.frag";
    program_ = load_render_program(vertex_shader_path, fragment_shader_path, gl);

    mvp_loc_ = require_uniform_location(program_, "uMVP", gl);
    solid_mode_loc_ = require_uniform_location(program_, "uUseSolidColor", gl);
    solid_color_loc_ = require_uniform_location(program_, "uSolidColor", gl);
    opacity_loc_ = require_uniform_location(program_, "uOpacity", gl);
    position_buffer_mode_loc_ = require_uniform_location(program_, "uUsePositionBuffer", gl);
    normal_lighting_mode_loc_ = require_uniform_location(program_, "uUseNormalLighting", gl);
    light_direction_loc_ = require_uniform_location(program_, "uLightDirectionWorld", gl);
    fill_light_direction_loc_ = require_uniform_location(program_, "uFillLightDirectionWorld", gl);
    ambient_strength_loc_ = require_uniform_location(program_, "uAmbientStrength", gl);
    diffuse_strength_loc_ = require_uniform_location(program_, "uDiffuseStrength", gl);
    fill_diffuse_strength_loc_ = require_uniform_location(program_, "uFillDiffuseStrength", gl);

    gl.glProgramUniform3f(program_,
                          light_direction_loc_,
                          light_direction_world.x,
                          light_direction_world.y,
                          light_direction_world.z);
    gl.glProgramUniform3f(program_,
                          fill_light_direction_loc_,
                          fill_light_direction_world.x,
                          fill_light_direction_world.y,
                          fill_light_direction_world.z);
    gl.glProgramUniform1f(program_, ambient_strength_loc_, ambient_strength);
    gl.glProgramUniform1f(program_, diffuse_strength_loc_, diffuse_strength);
    gl.glProgramUniform1f(program_, fill_diffuse_strength_loc_, fill_diffuse_strength);

    background_gradient_.initialize(shader_dir, gl);

    ground_grid_.initialize(gl);
}

void SceneRenderer::draw(const SceneState& scene,
                         const SceneGpuState& gpu_state,
                         const std::array<glm::mat4, 2>& placement_matrices,
                         const glm::mat4& mvp,
                         float character_opacity,
                         QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized() && gpu_state.is_initialized());

    // Background
    gl.glDisable(GL_DEPTH_TEST);
    background_gradient_.draw(gl);
    gl.glEnable(GL_DEPTH_TEST);

    // shader setting
    gl.glUseProgram(program_);
    gl.glProgramUniformMatrix4fv(program_, mvp_loc_, 1, GL_FALSE, glm::value_ptr(mvp));
    gl.glProgramUniform1i(program_, position_buffer_mode_loc_, 0);
    gl.glProgramUniform1i(program_, solid_mode_loc_, 1);
    gl.glProgramUniform1f(program_, opacity_loc_, 1.0f);

    // ground grid
    gl.glProgramUniform1i(program_, normal_lighting_mode_loc_, 0);
    const glm::vec3& ground_color = ground_grid_.color();
    gl.glProgramUniform3f(program_, solid_color_loc_, ground_color.r, ground_color.g, ground_color.b);
    gl.glDepthMask(GL_FALSE);
    ground_grid_.draw(gl);
    gl.glDepthMask(GL_TRUE);

    // garments
    gl.glProgramUniform1i(program_, normal_lighting_mode_loc_, 1);
    const auto& cloth_gpu_state = gpu_state.cloth_gpu_state();
    for (const GarmentObject& garment : scene.garments()) {
        const glm::vec3& color = garment.mesh.color;
        const glm::mat4 garment_mvp = mvp * placement_matrices[garment.layer];
        gl.glProgramUniform3f(program_, solid_color_loc_, color.r, color.g, color.b);
        gl.glProgramUniformMatrix4fv(program_, mvp_loc_, 1, GL_FALSE, glm::value_ptr(garment_mvp));
        cloth_gpu_state.draw_garment(garment.layer, gl);
    }

    // character
    gl.glProgramUniformMatrix4fv(program_, mvp_loc_, 1, GL_FALSE, glm::value_ptr(mvp));
    const bool character_transparent = character_opacity < 1.0f;
    if (character_transparent) {
        gl.glEnable(GL_BLEND);
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.glDepthMask(GL_FALSE);
    }

    gl.glProgramUniform1i(program_, position_buffer_mode_loc_, 1);
    gl.glProgramUniform1i(program_, solid_mode_loc_, 0);
    gl.glProgramUniform1f(program_, opacity_loc_, character_opacity);
    gpu_state.character_gpu_state().draw(gl);

    if (character_transparent) {
        gl.glDepthMask(GL_TRUE);
        gl.glDisable(GL_BLEND);
    }
}

void SceneRenderer::release(QOpenGLFunctions_4_5_Core& gl)
{
    ground_grid_.release(gl);
    background_gradient_.release(gl);
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

