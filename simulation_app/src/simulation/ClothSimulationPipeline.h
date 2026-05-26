#pragma once

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuResources;
class SceneState;

class ClothSimulationPipeline final {
public:
    ClothSimulationPipeline() = default;
    ClothSimulationPipeline(const ClothSimulationPipeline&) = delete;
    ClothSimulationPipeline& operator=(const ClothSimulationPipeline&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    bool step(const SceneState& scene, SceneGpuResources& gpu_state, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool initialized_ = false;
};
