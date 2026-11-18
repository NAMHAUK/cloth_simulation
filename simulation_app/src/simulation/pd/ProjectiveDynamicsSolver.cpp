#include "simulation/pd/ProjectiveDynamicsSolver.h"
#include "AttachmentConstraints.h"
#include "BendingConstraints.h"
#include "ClothIntegration.h"
#include "GroundConstraints.h"
#include "MeshNormals.h"
#include "PdGlobalSolve.h"
#include "PdRhs.h"
#include "StrainConstraints.h"
#include "gpu/pd/PdGpuState.h"

ProjectiveDynamicsSolver::ProjectiveDynamicsSolver(PdGpuState& pd_state,
                                                   float gravity,
                                                   float dt,
                                                   std::uint32_t iteration_count,
                                                   float attachment_surface_offset,
                                                   float ground_height)
    : pd_state_(pd_state),
      gravity_(gravity),
      dt_(dt),
      inv_dt_squared_(1.0f / (dt * dt)),
      iteration_count_(iteration_count),
      attachment_surface_offset_(attachment_surface_offset),
      ground_height_(ground_height)
{}

void ProjectiveDynamicsSolver::solve()
{
    PdClothGpuState& cloth_state = pd_state_.cloth_gpu_state();
    const std::uint32_t vertex_count = cloth_state.vertex_count();
    if (vertex_count == 0) {
        return;
    }

    const MappedMeshBuffers cloth_buffers = cloth_state.shared_buffers.map();
    const MappedMeshBuffers character_buffers = pd_state_.character_gpu_state().shared_buffers.map();

    integrate_cloth(cloth_buffers.positions, cloth_state, gravity_, dt_);
    compute_inertia_rhs(pd_state_.inertia_rhs_buffer(), cloth_buffers.positions, cloth_state, inv_dt_squared_);

    for (std::uint32_t iteration = 0; iteration < 30; ++iteration) {
        project_constraints(pd_state_.constraints().get<ConstraintIndex::Strain>(), cloth_buffers.positions);
        project_constraints(pd_state_.constraints().get<ConstraintIndex::Bending>(), cloth_buffers.positions);
        project_constraints(pd_state_.constraints().get<ConstraintIndex::Attachment>(),
                            character_buffers.positions,
                            character_buffers.triangle_vertex_indices,
                            attachment_surface_offset_);
        project_constraints(pd_state_.constraints().get<ConstraintIndex::Ground>(),
                            cloth_buffers.positions,
                            ground_height_);

        pd_state_.set_rhs(pd_state_.inertia_rhs_buffer());
        accumulate_constraint_rhs(pd_state_.constraints(), pd_state_.rhs_buffer(), vertex_count);

        solve_global(pd_state_.system_matrix(), pd_state_.rhs_buffer(), cloth_buffers.positions, vertex_count);
    }

    update_cloth_velocities(cloth_buffers.positions, cloth_state, dt_);
    update_normals(cloth_buffers, vertex_count, cloth_state.triangle_color_counts());

    pd_state_.character_gpu_state().shared_buffers.unmap();
    cloth_state.shared_buffers.unmap();
}
