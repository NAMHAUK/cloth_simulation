#pragma once

#include "rendering/BackgroundGradient.h"
#include "rendering/GroundGridMesh.h"

#include <array>
#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class SceneRenderer final
{
public:
    SceneRenderer() = default;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);

    void draw(const SceneState& scene,
              const SceneGpuState& gpu_state,
              const std::array<glm::mat4, 2>& placement_matrices,
              const glm::mat4& mvp,
              bool is_placement_active,
              QOpenGLFunctions_4_5_Core& gl);

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    BackgroundGradient background_gradient_;
    GroundGridMesh ground_grid_;

    GLuint program_ = 0;
    GLint mvp_loc_ = -1;
    GLint solid_mode_loc_ = -1;
    GLint solid_color_loc_ = -1;
    GLint opacity_loc_ = -1;
    GLint position_buffer_mode_loc_ = -1;
    GLint normal_lighting_mode_loc_ = -1;
    GLint light_direction_loc_ = -1;
    GLint fill_light_direction_loc_ = -1;
    GLint ambient_strength_loc_ = -1;
    GLint diffuse_strength_loc_ = -1;
    GLint fill_diffuse_strength_loc_ = -1;
};
