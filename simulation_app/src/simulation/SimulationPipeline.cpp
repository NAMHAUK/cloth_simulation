#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <algorithm>
#include <iostream>

#include <glm/vec3.hpp>

namespace {

constexpr std::uint32_t gpu_timing_log_interval = 200u;

float character_frame_time(std::uint64_t motion_step_index, std::uint32_t substep)
{
    if (simulation_settings::character_frame_stride == 0 || simulation_settings::substep_count == 0) {
        return 0.0f;
    }

    const float substep_fraction =
        static_cast<float>(std::min(substep + 1u, simulation_settings::substep_count)) /
        simulation_settings::substep_count;

    return (static_cast<float>(motion_step_index) + substep_fraction) /
           simulation_settings::character_frame_stride;
}

void update_character_substep_frame(const SceneState& scene,
                                    SceneGpuState& gpu_state,
                                    std::uint64_t motion_step_index,
                                    std::uint32_t substep,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    const float frame_time = character_frame_time(motion_step_index, substep);
    const CharacterFrameInterpolation interpolation = scene.character_frame_interpolation(frame_time);

    gpu_state.update_character_frame_interpolation(scene, interpolation, gl);
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
        body_vertex_cloth_face_collision_solver_.initialize(shader_paths.body_vertex_cloth_face_pair_generate_compute,
                                                            shader_paths.body_vertex_cloth_face_pair_accumulate_compute,
                                                            shader_paths.body_vertex_cloth_face_pair_apply_compute,
                                                            simulation_settings::character_collision_thickness,
                                                            simulation_settings::character_collision_max_correction_length,
                                                            simulation_settings::ignored_body_part_mask,
                                                            gl) &&
        cloth_edge_body_edge_collision_solver_.initialize(shader_paths.cloth_edge_body_edge_pair_generate_compute,
                                                          shader_paths.cloth_edge_body_edge_pair_accumulate_compute,
                                                          shader_paths.body_vertex_cloth_face_pair_apply_compute,
                                                          simulation_settings::character_collision_thickness,
                                                          simulation_settings::character_collision_max_correction_length,
                                                          simulation_settings::ignored_body_part_mask,
                                                          gl) &&
        cloth_vertex_body_face_collision_solver_.initialize(shader_paths.cloth_vertex_body_face_collision_compute,
                                                            simulation_settings::character_collision_thickness,
                                                            simulation_settings::character_collision_max_correction_length,
                                                            simulation_settings::ignored_body_part_mask,
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
#if CLOTH_SIM_SUBSTEP_GPU_TIMING
    substep_gpu_timer_.initialize("SimulationPipeline::substep", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool SimulationPipeline::prefit_garments(SceneState& scene, SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        std::cerr << "Cannot pre-fit garments before simulation pipeline initialization.\n";
        return false;
    }

    const auto views = collect_gpu_views(gpu_state);
    if (!garment_prefit_solver_.can_solve(views.cloth_motion, views.character_geometry, views.character_bvh)) {
        std::cerr << "Cannot pre-fit garments because required GPU buffers are missing.\n";
        return false;
    }

    for (std::uint32_t iteration = 0; iteration < simulation_settings::prefit_iteration_count; ++iteration) {
        garment_prefit_solver_.solve(views.cloth_motion, views.character_geometry, views.character_bvh, gl);
    }

    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
    gpu_state.update_mesh_normals(gl);
    return true;
}

bool SimulationPipeline::step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        return false;
    }

    const auto views = collect_gpu_views(gpu_state);
    if (!can_solve_constraint_iteration(views)) {
        return false;
    }

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    for (std::uint32_t substep = 0; substep < simulation_settings::substep_count; ++substep) {
#if CLOTH_SIM_SUBSTEP_GPU_TIMING
        const bool gpu_timing_started = substep_gpu_timer_.begin(gl);
#endif

        update_character_substep_frame(scene, gpu_state, motion_step_index, substep, gl);

        external_force_solver_.solve(views.cloth_motion,
                                     views.cloth_collision,
                                     substep_dt_,
                                     external_acceleration,
                                     simulation_settings::velocity_damping,
                                     gl);

        for (std::uint32_t iteration = 0; iteration < simulation_settings::solver_iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(views.cloth_motion, views.stretch_constraints, gl);
            bending_constraint_solver_.solve(views.cloth_motion, views.bending_constraints, gl);
            attachment_constraint_solver_.solve(views.cloth_motion, views.attachment_constraints, views.character_geometry, gl);
            body_vertex_cloth_face_collision_solver_.solve(views.cloth_motion,
                                                           views.cloth_collision,
                                                           views.cloth_topology,
                                                           views.character_vertices,
                                                           views.body_vertex_bvh,
                                                           views.collision_workspace,
                                                           gl);
            cloth_edge_body_edge_collision_solver_.solve(views.cloth_motion,
                                                         views.cloth_collision,
                                                         views.stretch_constraints,
                                                         views.character_vertices,
                                                         views.body_edge_bvh,
                                                         views.collision_workspace,
                                                         gl);
            cloth_vertex_body_face_collision_solver_.solve(views.cloth_motion,
                                                           views.cloth_collision,
                                                           views.character_geometry,
                                                           views.character_bvh,
                                                           gl);
            ground_collision_solver_.solve(views.cloth_motion, views.cloth_collision, gl);
        }

#if CLOTH_SIM_SUBSTEP_GPU_TIMING
        if (gpu_timing_started) {
            substep_gpu_timer_.end(gl);
        }
#endif
    }

    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
#if CLOTH_SIM_SUBSTEP_GPU_TIMING
    substep_gpu_timer_.release(gl);
#endif
    garment_prefit_solver_.release(gl);
    cloth_vertex_body_face_collision_solver_.release(gl);
    cloth_edge_body_edge_collision_solver_.release(gl);
    body_vertex_cloth_face_collision_solver_.release(gl);
    ground_collision_solver_.release(gl);
    attachment_constraint_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    external_force_solver_.release(gl);
    substep_dt_ = 0.0f;
    initialized_ = false;
}

SimulationPipeline::SimulationGpuViews SimulationPipeline::collect_gpu_views(const SceneGpuState& gpu_state)
{
    SimulationGpuViews views;
    views.cloth_motion = gpu_state.cloth_gpu_state().motion_buffer_view();
    views.cloth_collision = gpu_state.cloth_gpu_state().collision_state_buffer_view();
    views.cloth_topology = gpu_state.cloth_gpu_state().mesh_topology_resources();
    views.character_vertices = gpu_state.character_gpu_state().character_vertex_buffer_view();
    views.character_geometry = gpu_state.character_gpu_state().character_triangle_geometry_resources();
    views.character_bvh = gpu_state.character_gpu_state().character_bvh_resources();
    views.body_vertex_bvh = gpu_state.character_gpu_state().body_vertex_bvh_resources();
    views.body_edge_bvh = gpu_state.character_gpu_state().body_edge_bvh_resources();
    views.collision_workspace = gpu_state.collision_workspace_buffer_view();
    views.stretch_constraints = gpu_state.cloth_gpu_state().stretch_constraint_buffer_view();
    views.bending_constraints = gpu_state.cloth_gpu_state().bending_constraint_buffer_view();
    views.attachment_constraints = gpu_state.cloth_gpu_state().attachment_constraint_buffer_view();
    return views;
}

bool SimulationPipeline::can_solve_constraint_iteration(const SimulationGpuViews& views) const
{
    return stretch_constraint_solver_.can_solve(views.cloth_motion, views.stretch_constraints) &&
           bending_constraint_solver_.can_solve(views.cloth_motion, views.bending_constraints) &&
           (views.attachment_constraints.constraint_count == 0 ||
            attachment_constraint_solver_.can_solve(views.cloth_motion, views.attachment_constraints, views.character_geometry)) &&
           body_vertex_cloth_face_collision_solver_.can_solve(views.cloth_motion,
                                                              views.cloth_collision,
                                                              views.cloth_topology,
                                                              views.character_vertices,
                                                              views.body_vertex_bvh,
                                                              views.collision_workspace) &&
           cloth_edge_body_edge_collision_solver_.can_solve(views.cloth_motion,
                                                            views.cloth_collision,
                                                            views.stretch_constraints,
                                                            views.character_vertices,
                                                            views.body_edge_bvh,
                                                            views.collision_workspace) &&
           cloth_vertex_body_face_collision_solver_.can_solve(views.cloth_motion,
                                                              views.cloth_collision,
                                                              views.character_geometry,
                                                              views.character_bvh) &&
           ground_collision_solver_.can_solve(views.cloth_motion, views.cloth_collision);
}
