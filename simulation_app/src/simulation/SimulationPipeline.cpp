#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <iostream>

#include <glm/vec3.hpp>

namespace {

struct SimulationGpuViews final {
    ClothPositionBufferView cloth_position;
    TriangleGeometryResources character_geometry;
    MeshBvhResources character_bvh;
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;
    AttachmentConstraintBufferView attachment_constraints;
};

SimulationGpuViews collect_gpu_views(const SceneGpuState& gpu_state)
{
    SimulationGpuViews views;
    views.cloth_position = gpu_state.cloth_gpu_state().position_buffer_view();
    views.character_geometry = gpu_state.character_gpu_state().character_triangle_geometry_resources();
    views.character_bvh = gpu_state.character_gpu_state().character_bvh_resources();
    views.stretch_constraints = gpu_state.cloth_gpu_state().stretch_constraint_buffer_view();
    views.bending_constraints = gpu_state.cloth_gpu_state().bending_constraint_buffer_view();
    views.attachment_constraints = gpu_state.cloth_gpu_state().attachment_constraint_buffer_view();
    return views;
}

}

bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    substep_dt_ = simulation_settings::fixed_dt / static_cast<float>(simulation_settings::substep_count);

    const float floor_height = simulation_settings::ground_y + simulation_settings::ground_collision_offset;
    
    const bool solvers_initialized =
        external_force_solver_.initialize(shader_paths.cloth_external_force_compute, gl) &&
        stretch_constraint_solver_.initialize(shader_paths.cloth_stretch_constraint_compute, simulation_settings::stretch_stiffness, gl) &&
        bending_constraint_solver_.initialize(shader_paths.cloth_bending_constraint_compute, simulation_settings::bending_stiffness, gl) &&
        attachment_constraint_solver_.initialize(shader_paths.cloth_attachment_constraint_compute, simulation_settings::attachment_stiffness, gl) &&
        ground_collision_solver_.initialize(shader_paths.cloth_ground_collision_compute, floor_height, gl) &&
        character_collision_solver_.initialize(shader_paths.cloth_character_collision_compute,
                                               simulation_settings::character_collision_search_radius,
                                               simulation_settings::character_collision_thickness,
                                               gl) &&
        garment_prefit_solver_.initialize(shader_paths.garment_prefit_compute,
                                          simulation_settings::prefit_search_radius,
                                          simulation_settings::prefit_pushout_margin,
                                          gl);

    if (!solvers_initialized) {
        release(gl);
        return false;
    }

    initialized_ = true;
    return true;
}

bool SimulationPipeline::prefit_garments(SceneState& scene, SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        std::cerr << "Cannot pre-fit garments before simulation pipeline initialization.\n";
        return false;
    }

    gpu_state.update_character_frame(scene, gl);

    const auto views = collect_gpu_views(gpu_state);
    if (!garment_prefit_solver_.can_solve(views.cloth_position, views.character_geometry, views.character_bvh)) {
        std::cerr << "Cannot pre-fit garments because required GPU buffers are missing.\n";
        return false;
    }

    for (std::uint32_t iteration = 0; iteration < simulation_settings::prefit_iteration_count; ++iteration) {
        garment_prefit_solver_.solve(views.cloth_position, views.character_geometry, views.character_bvh, gl);
    }

    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
    gpu_state.update_mesh_normals(gl);
    return true;
}

bool SimulationPipeline::step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_count, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        return false;
    }

    scene.update_character_frame(motion_step_count, simulation_settings::character_frame_stride);
    gpu_state.update_character_frame(scene, gl);

    const auto views = collect_gpu_views(gpu_state);
    if (!can_solve_constraint_iteration(views.cloth_position, views.stretch_constraints, views.bending_constraints, views.character_geometry, views.character_bvh)) {
        return false;
    }

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    for (std::uint32_t substep = 0; substep < simulation_settings::substep_count; ++substep) {
        external_force_solver_.solve(views.cloth_position, substep_dt_, external_acceleration, gl);

        for (std::uint32_t iteration = 0; iteration < simulation_settings::solver_iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(views.cloth_position, views.stretch_constraints, gl);
            bending_constraint_solver_.solve(views.cloth_position, views.bending_constraints, gl);
            attachment_constraint_solver_.solve(views.cloth_position, views.attachment_constraints, views.character_geometry, gl);
            character_collision_solver_.solve(views.cloth_position, views.character_geometry, views.character_bvh, gl);
            ground_collision_solver_.solve(views.cloth_position, gl);
        }
    }

    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    garment_prefit_solver_.release(gl);
    character_collision_solver_.release(gl);
    ground_collision_solver_.release(gl);
    attachment_constraint_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    external_force_solver_.release(gl);
    substep_dt_ = 0.0f;
    initialized_ = false;
}

bool SimulationPipeline::can_solve_constraint_iteration(const ClothPositionBufferView& position_view,
                                                        const DistanceConstraintBufferView& stretch_constraint_view,
                                                        const DistanceConstraintBufferView& bending_constraint_view,
                                                        const TriangleGeometryResources& character_geometry,
                                                        const MeshBvhResources& character_bvh) const
{
    return stretch_constraint_solver_.can_solve(position_view, stretch_constraint_view) &&
           bending_constraint_solver_.can_solve(position_view, bending_constraint_view) &&
           character_collision_solver_.can_solve(position_view, character_geometry, character_bvh) &&
           ground_collision_solver_.can_solve(position_view);
}
