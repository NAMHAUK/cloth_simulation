#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"

#include <cassert>
#include <iostream>

#include <glm/vec3.hpp>

SimulationPipeline::SimulationPipeline(SimulationParams params)
    : params_(params),
      force_field_(params.external_force.gravity),
      external_force_solver_(params.external_force.velocity_damping,
                             params.external_force.reference_frame_inertia_scale,
                             params.external_force.reference_frame_max_acceleration,
                             params.external_force.reference_frame_max_angular_acceleration),
      stretch_constraint_solver_(params.constraints.stretch_stiffness),
      bending_constraint_solver_(params.constraints.bending_stiffness),
      attachment_constraint_solver_(params.constraints.attachment_stiffness),
      ground_collision_solver_(params.collisions.ground),
      cloth_body_collision_detector_(params.collisions.body.thickness),
      cloth_body_collision_solver_(params.collisions.body),
      cloth_cloth_collision_solver_(params.collisions.cloth),
      garment_prefit_solver_(params.prefit)
{}

bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_valid_simulation_params(params_)) {
        std::cerr << "Cannot initialize simulation pipeline with invalid parameters.\n";
        return false;
    }

    substep_dt_ = params_.step.dt() / static_cast<float>(params_.step.substep_count);
    inverse_substep_dt_ = 1.0f / substep_dt_;

    const bool solvers_initialized =
        cloth_bvh_bounds_updater_.initialize(shader_paths.cloth_bvh_bounds_update_compute, gl) &&
        external_force_solver_.initialize(shader_paths.cloth_external_force_compute, gl) &&
        stretch_constraint_solver_.initialize(shader_paths.cloth_stretch_constraint_compute, gl) &&
        bending_constraint_solver_.initialize(shader_paths.cloth_bending_constraint_compute, gl) &&
        attachment_constraint_solver_.initialize(shader_paths.cloth_attachment_constraint_compute, gl) &&
        ground_collision_solver_.initialize(shader_paths.cloth_ground_collision_compute, gl) &&
        cloth_body_collision_detector_.initialize(shader_paths.cloth_vertex_body_face_detect_compute,
                                                  shader_paths.cloth_edge_body_edge_detect_compute,
                                                  shader_paths.body_vertex_cloth_face_detect_compute,
                                                  shader_paths.collision_dispatch_size_compute,
                                                  gl) &&
        cloth_body_collision_solver_.initialize(shader_paths.cloth_vertex_body_face_accumulate_compute,
                                                shader_paths.cloth_edge_body_edge_accumulate_compute,
                                                shader_paths.body_vertex_cloth_face_accumulate_compute,
                                                shader_paths.cloth_body_collision_apply_compute,
                                                gl) &&
        cloth_cloth_collision_detector_.initialize(shader_paths.cloth_cloth_vertex_face_detect_compute,
                                                   shader_paths.collision_dispatch_size_compute,
                                                   gl) &&
        cloth_cloth_collision_solver_.initialize(shader_paths.cloth_cloth_vertex_face_accumulate_compute,
                                                 shader_paths.cloth_cloth_initial_layer_accumulate_compute,
                                                 shader_paths.cloth_body_triangle_id_build_compute,
                                                 shader_paths.cloth_cloth_collision_apply_compute,
                                                 gl) &&
        garment_prefit_solver_.initialize(shader_paths.garment_prefit_compute, gl);

    if (!solvers_initialized) {
        release(gl);
        return false;
    }

    initialized_ = true;
    return true;
}

bool SimulationPipeline::prefit_garments(const SceneState& scene,
                                         SceneGpuState& gpu_state,
                                         const std::vector<GarmentLayer>& layers,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_ || layers.empty()) {
        std::cerr << "Cannot pre-fit garments before simulation pipeline initialization or without garment "
                     "layers.\n";
        return false;
    }

    const auto views = gpu_state.simulation_view();
    std::vector<ElementRange> garment_vertex_ranges;
    garment_vertex_ranges.reserve(layers.size());
    for (GarmentLayer layer : layers) {
        const ElementRange& vertex_range = views.garment_vertex_ranges[layer];
        if (vertex_range.count == 0u || !garment_prefit_solver_.can_solve(views.cloth_motion,
                                                                          vertex_range,
                                                                          views.body_triangle_geometry,
                                                                          views.body_triangle_bvh)) {
            std::cerr << "Cannot pre-fit garment because required GPU buffers are missing.\n";
            return false;
        }
        garment_vertex_ranges.push_back(vertex_range);
    }

    const bool has_multiple_garments = scene.has_multiple_garments();
    if (has_multiple_garments && (!cloth_cloth_collision_detector_.can_detect(views) ||
                                  !cloth_cloth_collision_solver_.can_solve_initial(views) ||
                                  !cloth_cloth_collision_solver_.can_build_body_triangle_ids(views))) {
        std::cerr << "Cannot initialize cloth-cloth contacts because required GPU resources are invalid.\n";
        return false;
    }

    for (const ElementRange& vertex_range : garment_vertex_ranges) {
        for (std::uint32_t iteration = 0; iteration < params_.prefit.iteration_count; ++iteration) {
            garment_prefit_solver_.solve(views.cloth_motion,
                                         vertex_range,
                                         views.body_triangle_geometry,
                                         views.body_triangle_bvh,
                                         gl);
        }
    }

    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);

    if (has_multiple_garments) {
        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            update_cloth_bvh_bounds(views, params_.collisions.cloth.initial_detection_distance, gl);
            cloth_cloth_collision_detector_.detect(views, gl);
            cloth_cloth_collision_solver_.solve_initial(views, gl);
            gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
        }
    }

    if (has_multiple_garments) {
        cloth_cloth_collision_solver_.build_body_triangle_ids(views, gl);
    }

    gpu_state.update_mesh_normals(gl);
    return true;
}

void SimulationPipeline::step(SceneState& scene,
                              SceneGpuState& gpu_state,
                              std::uint64_t motion_step_index,
                              QOpenGLFunctions_4_5_Core& gl)
{
    if (scene.garments().empty()) {
        const float frame_time = params_.step.character_frame_time(motion_step_index + 1u, 0);
        const float frame_alpha = scene.character_frame_alpha(frame_time);
        gpu_state.update_character_pose(scene, frame_alpha, params_.collisions.body.thickness, gl);
        return;
    }

    const auto views = gpu_state.simulation_view();
    const bool has_multiple_garments = scene.has_multiple_garments();

    if (has_multiple_garments) {
        cloth_cloth_collision_solver_.build_body_triangle_ids(views, gl);
    }

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        const float frame_time = params_.step.character_frame_time(motion_step_index, substep + 1u);
        const float frame_alpha = scene.character_frame_alpha(frame_time);
        scene.update_reference_kinematics(frame_alpha, substep_dt_);

        gpu_state.update_character_pose(scene, frame_alpha, params_.collisions.body.thickness, gl);

        for (const GarmentObject& garment : scene.garments()) {
            const ElementRange& vertex_range = views.garment_vertex_ranges[garment.layer];
            assert(vertex_range.count != 0u);

            const Kinematics& reference_kinematics =
                scene.reference_kinematics(garment.mesh.garment_category);
            external_force_solver_.solve(views.cloth_motion,
                                         views.cloth_collision_pushout,
                                         views.cloth_contact_motion,
                                         vertex_range,
                                         substep_dt_,
                                         inverse_substep_dt_,
                                         external_acceleration,
                                         reference_kinematics,
                                         gl);
        }

        cloth_body_collision_detector_.detect(views, gl);

        if (has_multiple_garments) {
            update_cloth_bvh_bounds(views, params_.collisions.cloth.detection_distance(), gl);
            cloth_cloth_collision_detector_.detect(views, gl);
        }

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(views.cloth_motion, views.stretch_constraints, gl);
            bending_constraint_solver_.solve(views.cloth_motion, views.bending_constraints, gl);
            attachment_constraint_solver_.solve(views.cloth_motion,
                                                views.attachment_constraints,
                                                views.body_triangle_geometry,
                                                gl);
            cloth_body_collision_solver_.solve(views, gl);
            if (has_multiple_garments) {
                cloth_cloth_collision_solver_.solve(views, gl);
            }
            ground_collision_solver_.solve(views.cloth_motion,
                                           views.cloth_collision_pushout,
                                           views.cloth_contact_motion,
                                           gl);
        }
    }

    gpu_state.update_mesh_normals(gl);
}

void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    garment_prefit_solver_.release(gl);
    cloth_cloth_collision_solver_.release(gl);
    cloth_cloth_collision_detector_.release(gl);
    cloth_body_collision_solver_.release(gl);
    cloth_body_collision_detector_.release(gl);
    ground_collision_solver_.release(gl);
    attachment_constraint_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    external_force_solver_.release(gl);
    cloth_bvh_bounds_updater_.release(gl);
    substep_dt_ = 0.0f;
    inverse_substep_dt_ = 0.0f;
    initialized_ = false;
}

void SimulationPipeline::update_cloth_bvh_bounds(const SimulationGpuView& views,
                                                 float bounds_margin,
                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    cloth_bvh_bounds_updater_.update(views.cloth_motion,
                                     views.cloth_bvh,
                                     views.garment_vertex_ranges,
                                     bounds_margin,
                                     gl);
}
