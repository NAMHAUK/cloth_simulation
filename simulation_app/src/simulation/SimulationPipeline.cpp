#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <glm/vec3.hpp>

bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    if (!external_force_solver_.initialize(shader_paths.cloth_external_force_compute, gl)) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool SimulationPipeline::step(SceneState& scene,
                              SceneGpuState& gpu_state,
                              std::uint64_t simulation_step_count,
                              QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        return false;
    }

    if (scene.has_character()) {
        scene.update_character_frame(simulation_step_count, simulation_settings::character_frame_stride);
        gpu_state.sync_character_frame(scene);

        const auto position_view = gpu_state.cloth_gpu_state().position_buffer_view();
        const glm::vec3 external_acceleration = force_field_.external_acceleration();
        external_force_solver_.solve(position_view, simulation_settings::fixed_dt, external_acceleration, gl);
    }

    // Future GPU simulation order lives here:
    // constraints -> body collision -> cloth collision -> substep scheduling.
    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    external_force_solver_.release(gl);
    initialized_ = false;
}
