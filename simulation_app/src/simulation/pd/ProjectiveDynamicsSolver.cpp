#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "AttachmentConstraints.h"
#include "ClothIntegration.h"
#include "MeshNormals.h"
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

    const MappedMeshBuffers mesh_buffers = cloth_state.shared_buffers.map();

    integrate_cloth(mesh_buffers.positions, cloth_state, gravity_, dt_);
    compute_inertia_rhs(pd_state_.inertia_rhs_buffer(), mesh_buffers.positions, cloth_state, inv_dt_squared_);

    for (std::uint32_t iteration = 0; iteration < iteration_count_; ++iteration) {
        pd_state_.constraints().execute_each([positions = mesh_buffers.positions](const auto& constraints) {
            project_constraints(constraints.buffer, constraints.count, positions);
        });

        pd_state_.set_rhs(pd_state_.inertia_rhs_buffer());
        accumulate_constraint_rhs(pd_state_.constraints(), pd_state_.rhs_buffer());

        solve_global(pd_state_.system_matrix(), pd_state_.rhs_buffer(), mesh_buffers.positions, vertex_count);
    }

    update_cloth_velocities(mesh_buffers.positions, cloth_state, dt_);
    update_normals(mesh_buffers, vertex_count, cloth_state.triangle_color_counts());

    cloth_state.shared_buffers.unmap();
}
