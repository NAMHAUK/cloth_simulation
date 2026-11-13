#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "AttachmentConstraints.h"
#include "ClothIntegration.h"
#include "PdGlobalSolve.h"
#include "PdRhs.h"
#include "StretchConstraints.h"
#include "gpu/pd/PdGpuState.h"

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
    integrate_cloth(positions, cloth_state, gravity_, dt_);
    compute_inertia_rhs(pd_state_.inertia_rhs_buffer(), cloth_state, inv_dt_squared_);

    for (std::uint32_t iteration = 0; iteration < iteration_count_; ++iteration) {
        project_stretch_constraints(pd_state_.stretch_constraint_buffer());
        project_attachment_constraints(pd_state_.attachment_constraint_buffer(),
                                       pd_state_.attachment_constraint_count());

        pd_state_.set_rhs(pd_state_.inertia_rhs_buffer());
        accumulate_constraint_rhs(pd_state_.stretch_constraint_buffer(),
                                  pd_state_.stretch_constraint_count(),
                                  pd_state_.rhs_buffer());
        accumulate_constraint_rhs(pd_state_.attachment_constraint_buffer(),
                                  pd_state_.attachment_constraint_count(),
                                  pd_state_.rhs_buffer());

        solve_global(pd_state_.system_matrix(), pd_state_.rhs_buffer(), positions, vertex_count);
    }

    update_cloth_velocities(positions, cloth_state, dt_);

    cloth_state.unmap_shared_buffers();
}
