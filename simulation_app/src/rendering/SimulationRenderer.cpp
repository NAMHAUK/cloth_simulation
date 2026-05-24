#include "rendering/SimulationRenderer.h"

#include "gpu/SimulationGpuState.h"
#include "simulation/SimulationScene.h"

#include <iostream>

namespace {
constexpr GLuint character_animation_position_binding = 0;
}

bool SimulationRenderer::is_initialized() const
{
    return viewer_shader_.is_initialized();
}

bool SimulationRenderer::initialize(const std::filesystem::path& vertex_shader_path,
                                    const std::filesystem::path& fragment_shader_path,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    if (!viewer_shader_.load(vertex_shader_path, fragment_shader_path, gl)) {
        std::cerr << "Failed to create viewer shader program.\n";
        return false;
    }

    grid_gpu_state_.upload(gl);
    return true;
}

void SimulationRenderer::draw(const SimulationScene& scene,
                              const SimulationGpuState& gpu_state,
                              const glm::mat4& mvp,
                              QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || !gpu_state.is_initialized()) {
        return;
    }

    // shader setting
    viewer_shader_.bind(gl);
    viewer_shader_.set_mvp(mvp, gl);
    viewer_shader_.set_attribute_position_mode(gl);

    // grid
    if (grid_gpu_state_.initialized()) {
        viewer_shader_.set_solid_color(grid_gpu_state_.color(), gl);

        gl.glDepthMask(GL_FALSE);
        grid_gpu_state_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    // character
    const CharacterGpuState& character_gpu_state = gpu_state.character_gpu_state();
    if (scene.has_character() && character_gpu_state.is_initialized()) {
        character_gpu_state.bind_animation_positions(character_animation_position_binding, gl);
        viewer_shader_.set_character_animation_mode(
            character_gpu_state.current_frame_index(),
            character_gpu_state.vertex_count(),
            gl
        );
        viewer_shader_.set_vertex_color_mode(gl);
        character_gpu_state.draw(gl);
    }

    // garments
    viewer_shader_.set_attribute_position_mode(gl);
    const std::vector<GarmentSceneObject>& garments = scene.garments();
    for (const GarmentSceneObject& garment : garments) {
        const ClothGpuState* cloth_gpu_state = gpu_state.garment_gpu_state(garment.id);
        if (cloth_gpu_state == nullptr || !garment.visible || !cloth_gpu_state->is_initialized()) {
            continue;
        }

        viewer_shader_.set_solid_color(garment.mesh.color, gl);
        cloth_gpu_state->draw(gl);
    }
}

void SimulationRenderer::release(QOpenGLFunctions_4_5_Core& gl)
{
    grid_gpu_state_.release(gl);
    viewer_shader_.release(gl);
}
