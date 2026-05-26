#include "rendering/SceneRenderer.h"

#include "gpu/scene/SceneGpuResources.h"
#include "scene/SceneState.h"

#include <iostream>

#include <glm/geometric.hpp>

namespace {
constexpr GLuint character_animation_position_binding = 0;
constexpr GLuint vertex_normal_binding = 1;

const glm::vec3 light_direction_world = glm::normalize(glm::vec3{-0.4f, 0.8f, 0.3f});
constexpr float ambient_strength = 0.35f;
constexpr float diffuse_strength = 0.65f;
}

bool SceneRenderer::is_initialized() const
{
    return viewer_shader_.is_initialized();
}

bool SceneRenderer::initialize(const std::filesystem::path& vertex_shader_path,
                                    const std::filesystem::path& fragment_shader_path,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    if (!viewer_shader_.load(vertex_shader_path, fragment_shader_path, gl)) {
        std::cerr << "Failed to create viewer shader program.\n";
        return false;
    }
    if (!background_gradient_.initialize(gl)) {
        std::cerr << "Failed to create background gradient.\n";
        return false;
    }

    ground_grid_.upload(gl);
    return true;
}

void SceneRenderer::draw(const SceneState& scene,
                              const SceneGpuResources& gpu_state,
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
    if (ground_grid_.initialized()) {
        viewer_shader_.set_normal_lighting_enabled(false, gl);
        viewer_shader_.set_solid_color(ground_grid_.color(), gl);
        gl.glDepthMask(GL_FALSE);
        ground_grid_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    // character
    const CharacterGpuResources& character_gpu_state = gpu_state.character_gpu_state();
    if (scene.has_character() && character_gpu_state.is_initialized()) {
        character_gpu_state.bind_animation_positions(character_animation_position_binding, gl);
        character_gpu_state.bind_vertex_normals(vertex_normal_binding, gl);
        viewer_shader_.set_character_animation_mode(
            character_gpu_state.current_frame_index(),
            character_gpu_state.vertex_count(),
            gl
        );
        viewer_shader_.set_vertex_color_mode(gl);
        viewer_shader_.set_normal_lighting_enabled(true, gl);
        character_gpu_state.draw(gl);
    }

    // garments
    viewer_shader_.set_attribute_position_mode(gl);
    viewer_shader_.set_normal_lighting_enabled(true, gl);
    const std::vector<GarmentSceneObject>& garments = scene.garments();
    for (const GarmentSceneObject& garment : garments) {
        const ClothGpuResources* cloth_gpu_state = gpu_state.garment_gpu_state(garment.id);
        if (cloth_gpu_state == nullptr || !garment.visible || !cloth_gpu_state->is_initialized()) {
            continue;
        }

        cloth_gpu_state->bind_vertex_normals(vertex_normal_binding, gl);
        viewer_shader_.set_solid_color(garment.mesh.color, gl);
        cloth_gpu_state->draw(gl);
    }
}

void SceneRenderer::release(QOpenGLFunctions_4_5_Core& gl)
{
    ground_grid_.release(gl);
    background_gradient_.release(gl);
    viewer_shader_.release(gl);
}
