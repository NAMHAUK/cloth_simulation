#pragma once

#include "simulation/SimulationForceField.h"
#include "simulation/cloth/ClothExternalForceSolver.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
struct ShaderPaths;

class SimulationPipeline final {
public:
    SimulationPipeline() = default;
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    bool is_initialized() const;
    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool step(SceneState& scene,
              SceneGpuState& gpu_state,
              std::uint64_t simulation_step_count,
              QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    SimulationForceField force_field_;
    ClothExternalForceSolver external_force_solver_;
    bool initialized_ = false;
};
