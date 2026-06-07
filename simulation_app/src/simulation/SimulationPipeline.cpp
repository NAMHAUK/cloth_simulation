#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/MeshBufferResources.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <glm/vec3.hpp>

namespace {

struct SimulationGpuViews final {
    ClothPositionBufferView cloth_position;
    MeshTopologyResources character_topology;
    StretchConstraintBufferView stretch_constraints;
    BendingConstraintBufferView bending_constraints;
};

SimulationGpuViews collect_gpu_views(const SceneGpuState& gpu_state)
{
    SimulationGpuViews views;
    views.cloth_position = gpu_state.cloth_gpu_state().position_buffer_view();
    views.character_topology = gpu_state.character_gpu_state().mesh_topology_resources();
    views.stretch_constraints = gpu_state.cloth_gpu_state().stretch_constraint_buffer_view();
    views.bending_constraints = gpu_state.cloth_gpu_state().bending_constraint_buffer_view();
    return views;
}

}


bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    const float floor_height = simulation_settings::ground_y + simulation_settings::ground_collision_offset;
    
    const bool solvers_initialized =
        external_force_solver_.initialize(shader_paths.cloth_external_force_compute, gl) &&
        stretch_constraint_solver_.initialize(shader_paths.cloth_stretch_constraint_compute, gl) &&
        bending_constraint_solver_.initialize(shader_paths.cloth_bending_constraint_compute, gl) &&
        ground_collision_solver_.initialize(shader_paths.cloth_ground_collision_compute, floor_height, gl) &&
        character_collision_solver_.initialize(shader_paths.cloth_character_collision_compute, simulation_settings::character_collision_thickness, gl);

    if (!solvers_initialized) {
        release(gl);
        return false;
    }

    initialized_ = true;
    return true;
}

bool SimulationPipeline::step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_count, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        return false;
    }

    scene.update_character_frame(motion_step_count, simulation_settings::character_frame_stride);
    gpu_state.update_character_frame(scene);

    const bool has_character = scene.has_character();
    const auto views = collect_gpu_views(gpu_state);
    if (!can_solve_constraint_iteration(views.cloth_position, views.stretch_constraints, views.bending_constraints, views.character_topology, has_character)) {
        return false;
    }

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    external_force_solver_.solve(views.cloth_position, simulation_settings::fixed_dt, external_acceleration, gl);

    for (std::uint32_t iteration = 0; iteration < simulation_settings::solver_iteration_count; ++iteration) {
        stretch_constraint_solver_.solve(views.cloth_position, views.stretch_constraints, simulation_settings::stretch_stiffness, gl);
        bending_constraint_solver_.solve(views.cloth_position, views.bending_constraints, simulation_settings::bending_stiffness, gl);
        if (has_character) {
            character_collision_solver_.solve(views.cloth_position, views.character_topology, gl);
        }
        ground_collision_solver_.solve(views.cloth_position, gl);
    }

    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    character_collision_solver_.release(gl);
    ground_collision_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    external_force_solver_.release(gl);
    initialized_ = false;
}

bool SimulationPipeline::can_solve_constraint_iteration(const ClothPositionBufferView& position_view,
                                                        const StretchConstraintBufferView& stretch_constraint_view,
                                                        const BendingConstraintBufferView& bending_constraint_view,
                                                        const MeshTopologyResources& character_topology,
                                                        bool has_character) const
{
    return stretch_constraint_solver_.can_solve(position_view, stretch_constraint_view, simulation_settings::stretch_stiffness) &&
           bending_constraint_solver_.can_solve(position_view, bending_constraint_view, simulation_settings::bending_stiffness) &&
           (!has_character || character_collision_solver_.can_solve(position_view, character_topology)) &&
           ground_collision_solver_.can_solve(position_view);
}