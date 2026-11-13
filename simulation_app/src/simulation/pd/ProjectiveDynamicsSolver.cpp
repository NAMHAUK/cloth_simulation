#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "ClothIntegration.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

ProjectiveDynamicsSolver::ProjectiveDynamicsSolver(PdGpuState& pd_state, float gravity, float dt)
    : pd_state_(pd_state),
      gravity_(gravity),
      dt_(dt)
{}

void ProjectiveDynamicsSolver::solve()
{
    PdClothGpuState& cloth_state = pd_state_.cloth_gpu_state();
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    if (vertex_count == 0) {
        return;
    }

    float4* positions = cloth_state.map_shared_buffers();
    CUDA_CHECK(integrate_cloth(positions,
                               cloth_state.previous_position_buffer(),
                               cloth_state.predicted_position_buffer(),
                               cloth_state.velocity_buffer(),
                               vertex_count,
                               gravity_,
                               dt_));

    cloth_state.unmap_shared_buffers();
}
