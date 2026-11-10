#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "ClothIntegration.h"
#include "gpu/scene/PdGpuState.h"
#include "utils/CudaUtils.h"

ProjectiveDynamicsSolver::ProjectiveDynamicsSolver(const PdGpuState& pd_state, float gravity)
    : pd_state_(pd_state),
      gravity_(gravity)
{}

void ProjectiveDynamicsSolver::initialize(float substep_dt)
{
    substep_dt_ = substep_dt;
}

void ProjectiveDynamicsSolver::solve()
{
    const PdClothGpuState& cloth_state = pd_state_.cloth_gpu_state();
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    if (vertex_count == 0) {
        return;
    }

    float4* positions = cloth_state.map_shared_buffers();
    const cudaError_t result = integrate_cloth(positions,
                                               cloth_state.velocities(),
                                               vertex_count,
                                               gravity_,
                                               substep_dt_);
    cloth_state.unmap_shared_buffers();
    CUDA_CHECK(result);
}

void ProjectiveDynamicsSolver::release()
{
    substep_dt_ = 0.0f;
}
