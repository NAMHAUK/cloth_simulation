#pragma once

#include "rendering/BackgroundGradient.h"
#include "rendering/GroundGridMesh.h"
#include "rendering/SceneRenderShader.h"

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
struct ShaderPaths;

class RenderPipeline final {
public:
    RenderPipeline() = default;
    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    bool is_initialized() const;

    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    void draw(const SceneState& scene,
              const SceneGpuState& gpu_state,
              const glm::mat4& mvp,
              float character_opacity,
              QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    BackgroundGradient background_gradient_;
    GroundGridMesh ground_grid_;
    SceneRenderShader viewer_shader_;
};
