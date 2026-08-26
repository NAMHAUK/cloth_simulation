#include "rendering/RenderPipeline.h"

#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"

#include <glm/geometric.hpp>

namespace {
constexpr GLuint character_position_binding = 0;
constexpr GLuint vertex_normal_binding = 1;

const glm::vec3 light_direction_world = glm::normalize(glm::vec3{-0.4f, 0.8f, 0.3f});
const glm::vec3 fill_light_direction_world = glm::normalize(glm::vec3{0.3f, 0.6f, -0.8f});
constexpr float ambient_strength = 0.35f;
constexpr float diffuse_strength = 0.65f;
constexpr float fill_diffuse_strength = 0.35f;
}

bool RenderPipeline::is_initialized() const
{
    return viewer_shader_.is_initialized();
}

void RenderPipeline::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    viewer_shader_.load(shader_dir, gl);
    background_gradient_.initialize(shader_dir, gl);

    ground_grid_.upload(gl);
}

void RenderPipeline::draw(const SceneState& scene,
                          const SceneGpuState& gpu_state,
                          const std::array<glm::mat4, 2>& placement_matrices,
                          const glm::mat4& mvp,
                          float character_opacity,
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
    viewer_shader_.set_lighting(light_direction_world,
                                fill_light_direction_world,
                                ambient_strength,
                                diffuse_strength,
                                fill_diffuse_strength,
                                gl);
    viewer_shader_.set_attribute_position_mode(gl);
    viewer_shader_.set_opacity(1.0f, gl);

    // ground grid
    if (ground_grid_.is_initialized()) {
        viewer_shader_.set_normal_lighting_enabled(false, gl);
        viewer_shader_.set_solid_color(ground_grid_.color(), gl);
        gl.glDepthMask(GL_FALSE);
        ground_grid_.draw(gl);
        gl.glDepthMask(GL_TRUE);
    }

    // garments
    viewer_shader_.set_attribute_position_mode(gl);
    viewer_shader_.set_normal_lighting_enabled(true, gl);
    const ClothGpuState& cloth_gpu_state = gpu_state.cloth_gpu_state();
    if (cloth_gpu_state.is_initialized()) {
        cloth_gpu_state.bind_vertex_normals(vertex_normal_binding, gl);
        const std::vector<GarmentObject>& garments = scene.garments();
        for (const GarmentObject& garment : garments) {
            viewer_shader_.set_solid_color(garment.mesh.color, gl);
            viewer_shader_.set_mvp(mvp * placement_matrices[garment.layer], gl);
            cloth_gpu_state.draw_garment(garment.layer, gl);
        }
    }

    // character
    const CharacterGpuState& character_gpu_state = gpu_state.character_gpu_state();
    viewer_shader_.set_mvp(mvp, gl);
    const bool character_transparent = character_opacity < 1.0f;
    if (character_transparent) {
        gl.glEnable(GL_BLEND);
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.glDepthMask(GL_FALSE);
    }

    character_gpu_state.bind_current_positions(character_position_binding, gl);
    character_gpu_state.bind_vertex_normals(vertex_normal_binding, gl);
    viewer_shader_.set_character_position_buffer_mode(gl);
    viewer_shader_.set_vertex_color_mode(gl);
    viewer_shader_.set_normal_lighting_enabled(true, gl);
    viewer_shader_.set_opacity(character_opacity, gl);
    character_gpu_state.draw(gl);

    if (character_transparent) {
        gl.glDepthMask(GL_TRUE);
        gl.glDisable(GL_BLEND);
    }
}

void RenderPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    ground_grid_.release(gl);
    background_gradient_.release(gl);
    viewer_shader_.release(gl);
}
