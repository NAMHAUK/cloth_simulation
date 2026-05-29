#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;

class SimulationPipeline final {
public:
    SimulationPipeline() = default;
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    bool step(SceneState& scene,
              SceneGpuState& gpu_state,
              std::uint64_t simulation_step_count,
              QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool initialized_ = false;
};
