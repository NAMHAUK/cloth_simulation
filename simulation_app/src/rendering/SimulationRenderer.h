#pragma once

#include "gpu/GridGpuState.h"
#include "rendering/ViewerShader.h"

#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SimulationGpuState;
class SimulationScene;

class SimulationRenderer final {
public:
    SimulationRenderer() = default;
    SimulationRenderer(const SimulationRenderer&) = delete;
    SimulationRenderer& operator=(const SimulationRenderer&) = delete;

    bool is_initialized() const;

    bool initialize(const std::filesystem::path& vertex_shader_path,
                    const std::filesystem::path& fragment_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    void draw(const SimulationScene& scene,
              const SimulationGpuState& gpu_state,
              const glm::mat4& mvp,
              QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    ViewerShader viewer_shader_;
    GridGpuState grid_gpu_state_;
};
