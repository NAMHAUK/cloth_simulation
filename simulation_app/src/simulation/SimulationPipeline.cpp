#include "simulation/SimulationPipeline.h"

#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(QOpenGLFunctions_4_5_Core&)
{
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

    scene.update_character_frame(simulation_step_count, simulation_settings::character_frame_stride);
    gpu_state.sync_character_frame(scene);

    // Future GPU simulation order lives here:
    // external forces -> integration -> constraints -> body collision -> cloth collision -> substep scheduling.
    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core&)
{
    initialized_ = false;
}
