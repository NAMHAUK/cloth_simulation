#include "simulation/ClothSimulationPipeline.h"

#include "gpu/scene/SceneGpuResources.h"
#include "scene/SceneState.h"

bool ClothSimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool ClothSimulationPipeline::initialize(QOpenGLFunctions_4_5_Core&)
{
    initialized_ = true;
    return true;
}

bool ClothSimulationPipeline::step(const SceneState&, SceneGpuResources&, QOpenGLFunctions_4_5_Core&)
{
    if (!initialized_) {
        return false;
    }

    // Future GPU simulation order lives here:
    // external forces -> integration -> constraints -> body collision -> cloth collision -> substep scheduling.
    return false;
}

void ClothSimulationPipeline::release(QOpenGLFunctions_4_5_Core&)
{
    initialized_ = false;
}
