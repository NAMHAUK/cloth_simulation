#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "ClothIntegration.h"
#include "PdGlobalSolve.h"
#include "PdRhs.h"
#include "StretchConstraints.h"
#include "gpu/pd/PdGpuState.h"
#include "utils/CudaUtils.h"

ProjectiveDynamicsSolver::ProjectiveDynamicsSolver(PdGpuState& pd_state,
                                                   float gravity,
                                                   float dt,
                                                   std::uint32_t iteration_count)
    : pd_state_(pd_state),
      gravity_(gravity),
      dt_(dt),
      inv_dt_squared_(1.0f / (dt * dt)),
      iteration_count_(iteration_count)
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

    for (std::uint32_t iteration = 0; iteration < iteration_count_; ++iteration) {
        project_stretch_constraints(pd_state_.constraint_p_buffer());

        compute_inertia_rhs(pd_state_.rhs_buffer(),
                            cloth_state.mass_buffer(),
                            cloth_state.predicted_position_buffer(),
                            vertex_count,
                            inv_dt_squared_);

        accumulate_constraint_rhs(pd_state_.constraint_matrix_buffer(),
                                  pd_state_.constraint_count(),
                                  pd_state_.rhs_buffer());

        solve_global(pd_state_.system_matrix(), pd_state_.rhs_buffer(), positions, vertex_count);
    }

    cloth_state.unmap_shared_buffers();
}
