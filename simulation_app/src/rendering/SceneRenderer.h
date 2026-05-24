#pragma once

#include "rendering/GroundMesh.h"
#include "rendering/SceneRenderShader.h"

#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuResources;
class SceneState;

class SceneRenderer final {
public:
    SceneRenderer() = default;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    bool is_initialized() const;

    bool initialize(const std::filesystem::path& vertex_shader_path,
                    const std::filesystem::path& fragment_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    void draw(const SceneState& scene,
              const SceneGpuResources& gpu_state,
              const glm::mat4& mvp,
              QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    SceneRenderShader viewer_shader_;
    GroundMesh grid_gpu_state_;
};
