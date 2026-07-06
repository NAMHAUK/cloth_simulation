#include "rendering/RenderPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"

#include <iostream>

#include <glm/geometric.hpp>

namespace {
constexpr GLuint character_position_binding = 0;
constexpr GLuint vertex_normal_binding = 1;

const glm::vec3 light_direction_world = glm::normalize(glm::vec3{-0.4f, 0.8f, 0.3f});
constexpr float ambient_strength = 0.35f;
constexpr float diffuse_strength = 0.65f;
}

bool RenderPipeline::is_initialized() const
{
    return viewer_shader_.is_initialized();
}

bool RenderPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    if (!viewer_shader_.load(shader_paths.viewer_vertex, shader_paths.viewer_fragment, gl)) {
        std::cerr << "Failed to create viewer shader program.\n";
        return false;
    }
    if (!background_gradient_.initialize(shader_paths.background_vertex, shader_paths.background_fragment, gl)) {
        std::cerr << "Failed to create background gradient.\n";
        return false;
    }

    ground_grid_.upload(gl);
    return true;
}

void RenderPipeline::draw(const SceneState& scene,
                              const SceneGpuState& gpu_state,
                              const glm::mat4& mvp,
                              QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !gpu_state.is_initialized()) {
        return;
    }

    if (background_gradient_.is_initialized()) {
        gl.glDepthMask(GL_FALSE);
        gl.glDisable(GL_DEPTH_TEST);
        background_gradient_.draw(gl);

        gl.glEnable(GL_DEPTH_TEST);
        gl.glDepthMask(GL_TRUE);
    }

    // shader setting
    viewer_shader_.bind(gl);
    viewer_shader_.set_mvp(mvp, gl);
    viewer_shader_.set_lighting(light_direction_world, ambient_strength, diffuse_strength, gl);
    viewer_shader_.set_attribute_position_mode(gl);

    // ground grid
    if (ground_grid_.is_initialized()) {
        viewer_shader_.set_normal_lighting_enabled(false, gl);
        viewer_shader_.set_solid_color(ground_grid_.color(), gl);
        gl.glDepthMask(GL_FALSE);
        ground_grid_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    // character
    const CharacterGpuResources& character_gpu_state = gpu_state.character_gpu_state();
    if (character_gpu_state.is_initialized()) {
        character_gpu_state.bind_current_positions(character_position_binding, gl);
        character_gpu_state.bind_vertex_normals(vertex_normal_binding, gl);
        viewer_shader_.set_character_position_buffer_mode(gl);
        viewer_shader_.set_vertex_color_mode(gl);
        viewer_shader_.set_normal_lighting_enabled(true, gl);
        character_gpu_state.draw(gl);
    }

    // garments
    viewer_shader_.set_attribute_position_mode(gl);
    viewer_shader_.set_normal_lighting_enabled(true, gl);
    const ClothGpuResources& cloth_gpu_state = gpu_state.cloth_gpu_state();
    if (cloth_gpu_state.is_initialized()) {
        cloth_gpu_state.bind_vertex_normals(vertex_normal_binding, gl);
    }
    const std::vector<GarmentObject>& garments = scene.garments();
    for (const GarmentObject& garment : garments) {
        if (!garment.visible || !cloth_gpu_state.is_initialized()) {
            continue;
        }

        viewer_shader_.set_solid_color(garment.mesh.color, gl);
        cloth_gpu_state.draw_garment(garment.id, gl);
    }
}

void RenderPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    ground_grid_.release(gl);
    background_gradient_.release(gl);
    viewer_shader_.release(gl);
}
