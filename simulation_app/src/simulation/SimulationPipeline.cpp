#include "simulation/SimulationPipeline.h"

#include "app/ProjectPaths.h"
#include "gpu/scene/SceneGpuState.h"
#include "scene/SceneState.h"
#include "simulation/SimulationSettings.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float angular_velocity_epsilon = 1.0e-8f;

const GarmentBufferRanges* find_garment_range(
    const std::vector<GarmentBufferRanges>& garment_ranges,
    std::uint32_t garment_id)
{
    const auto iter = std::find_if(garment_ranges.begin(), garment_ranges.end(),
        [garment_id](const GarmentBufferRanges& range) {
            return range.id == garment_id;
        });
    return iter == garment_ranges.end() ? nullptr : &(*iter);
}

float character_frame_time(std::uint64_t motion_step_index, std::int32_t substep_boundary)
{
    if (simulation_settings::character_frame_stride == 0 || simulation_settings::substep_count == 0) {
        return 0.0f;
    }

    const float substep_fraction = static_cast<float>(substep_boundary) /
                                   simulation_settings::substep_count;
    const float frame_time = (static_cast<float>(motion_step_index) + substep_fraction) /
                             simulation_settings::character_frame_stride;

    return std::max(frame_time, 0.0f);
}

void update_character_substep_frame(const SceneState& scene,
                                    SceneGpuState& gpu_state,
                                    float frame_time,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    const CharacterFrameInterpolation interpolation = scene.character_frame_interpolation(frame_time);

    gpu_state.update_character_frame_interpolation(scene, interpolation, gl);
}

glm::vec3 clamp_vector_length(const glm::vec3& value, float maximum_length)
{
    const float length = glm::length(value);
    return length > maximum_length ? value * (maximum_length / length) : value;
}

glm::vec3 angular_velocity(const glm::quat& start_orientation,
                           const glm::quat& end_orientation,
                           float dt)
{
    glm::quat delta = glm::normalize(end_orientation * glm::conjugate(start_orientation));
    if (delta.w < 0.0f) {
        delta = -delta;
    }

    const glm::vec3 vector{delta.x, delta.y, delta.z};
    const float vector_length = glm::length(vector);
    if (vector_length <= angular_velocity_epsilon || dt <= 0.0f) {
        return glm::vec3{0.0f};
    }

    const float angle = 2.0f * std::atan2(vector_length, std::clamp(delta.w, -1.0f, 1.0f));
    return vector * (angle / (vector_length * dt));
}

ReferenceFrameMotion make_reference_frame_motion(const CharacterReferenceFrame& previous_frame,
                                                 const CharacterReferenceFrame& start_frame,
                                                 const CharacterReferenceFrame& end_frame,
                                                 float dt)
{
    const glm::vec3 previous_velocity = (start_frame.position - previous_frame.position) / dt;
    const glm::vec3 velocity = (end_frame.position - start_frame.position) / dt;
    const glm::vec3 acceleration = clamp_vector_length(
        (velocity - previous_velocity) / dt,
        simulation_settings::reference_frame_max_acceleration
    );
    const glm::vec3 previous_angular_velocity =
        angular_velocity(previous_frame.orientation, start_frame.orientation, dt);
    const glm::vec3 current_angular_velocity =
        angular_velocity(start_frame.orientation, end_frame.orientation, dt);
    const glm::vec3 angular_acceleration = clamp_vector_length(
        (current_angular_velocity - previous_angular_velocity) / dt,
        simulation_settings::reference_frame_max_angular_acceleration
    );

    return {
        start_frame.position,
        end_frame.position,
        glm::mat3_cast(glm::normalize(end_frame.orientation * glm::conjugate(start_frame.orientation))),
        previous_velocity,
        acceleration,
        previous_angular_velocity,
        angular_acceleration
    };
}

ReferenceFrameMotion sample_reference_motion(const SceneState& scene,
                                             GarmentCategory garment_category,
                                             float previous_frame_time,
                                             float start_frame_time,
                                             float end_frame_time,
                                             float dt)
{
    return make_reference_frame_motion(
        scene.interpolated_character_reference_frame(previous_frame_time, garment_category),
        scene.interpolated_character_reference_frame(start_frame_time, garment_category),
        scene.interpolated_character_reference_frame(end_frame_time, garment_category),
        dt
    );
}
}

bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

bool SimulationPipeline::initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    substep_dt_ = simulation_settings::fixed_dt / static_cast<float>(simulation_settings::substep_count);
    inverse_substep_dt_ = 1.0f / substep_dt_;

    const bool solvers_initialized =
        cloth_bvh_bounds_updater_.initialize(shader_paths.cloth_bvh_bounds_update_compute, gl) &&
        external_force_solver_.initialize(shader_paths.cloth_external_force_compute, gl) &&
        stretch_constraint_solver_.initialize(shader_paths.cloth_stretch_constraint_compute, simulation_settings::stretch_stiffness, gl) &&
        bending_constraint_solver_.initialize(shader_paths.cloth_bending_constraint_compute, simulation_settings::bending_stiffness, gl) &&
        attachment_constraint_solver_.initialize(shader_paths.cloth_attachment_constraint_compute, simulation_settings::attachment_stiffness, gl) &&
        ground_collision_solver_.initialize(shader_paths.cloth_ground_collision_compute,
                                            simulation_settings::ground_collision_height,
                                            simulation_settings::static_friction,
                                            simulation_settings::dynamic_friction,
                                            gl) &&
        cloth_body_collision_detector_.initialize(shader_paths.cloth_vertex_body_face_detect_compute,
                                                  shader_paths.cloth_edge_body_edge_detect_compute,
                                                  shader_paths.body_vertex_cloth_face_detect_compute,
                                                  shader_paths.collision_dispatch_size_compute,
                                                  simulation_settings::body_collision_thickness,
                                                  gl) &&
        cloth_body_collision_solver_.initialize(shader_paths.cloth_vertex_body_face_accumulate_compute,
                                                shader_paths.cloth_edge_body_edge_accumulate_compute,
                                                shader_paths.body_vertex_cloth_face_accumulate_compute,
                                                shader_paths.cloth_body_collision_apply_compute,
                                                simulation_settings::body_collision_thickness,
                                                simulation_settings::body_collision_max_correction_length,
                                                simulation_settings::static_friction,
                                                simulation_settings::dynamic_friction,
                                                gl) &&
        cloth_cloth_collision_detector_.initialize(shader_paths.cloth_cloth_vertex_face_detect_compute,
                                                   shader_paths.collision_dispatch_size_compute,
                                                   gl) &&
        cloth_cloth_collision_solver_.initialize(shader_paths.cloth_cloth_vertex_face_accumulate_compute,
                                                 shader_paths.cloth_cloth_initial_layer_accumulate_compute,
                                                 shader_paths.cloth_body_triangle_id_build_compute,
                                                 shader_paths.cloth_cloth_collision_apply_compute,
                                                 simulation_settings::cloth_collision_thickness,
                                                 simulation_settings::cloth_collision_stiffness,
                                                 simulation_settings::cloth_collision_max_correction_length,
                                                 simulation_settings::cloth_collision_body_search_radius,
                                                 gl) &&
        garment_prefit_solver_.initialize(shader_paths.garment_prefit_compute,
                                          simulation_settings::prefit_surface_search_radius,
                                          simulation_settings::prefit_pushout_margin,
                                          gl);

    if (!solvers_initialized) {
        release(gl);
        return false;
    }

    initialized_ = true;
    return true;
}

bool SimulationPipeline::prefit_garments(const SceneState& scene,
                                         SceneGpuState& gpu_state,
                                         const std::vector<std::uint32_t>& garment_ids,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_ || garment_ids.empty()) {
        std::cerr << "Cannot pre-fit garments before simulation pipeline initialization or without garment ids.\n";
        return false;
    }

    const auto views = collect_gpu_views(gpu_state);
    if (views.garment_buffer_ranges == nullptr) {
        std::cerr << "Cannot pre-fit garment because garment buffer ranges are missing.\n";
        return false;
    }

    std::vector<const GarmentBufferRanges*> garment_ranges;
    garment_ranges.reserve(garment_ids.size());
    for (std::uint32_t garment_id : garment_ids) {
        const GarmentBufferRanges* garment_range = find_garment_range(*views.garment_buffer_ranges, garment_id);
        if (garment_range == nullptr ||
            !garment_prefit_solver_.can_solve(views.cloth_motion,
                                             *garment_range,
                                             views.body_triangle_geometry,
                                             views.body_triangle_bvh)) {
            std::cerr << "Cannot pre-fit garment because required GPU buffers are missing.\n";
            return false;
        }
        garment_ranges.push_back(garment_range);
    }

    const bool has_multiple_garments = scene.has_multiple_garments();
    if (has_multiple_garments &&
        (!cloth_cloth_collision_detector_.can_detect(views) ||
         !cloth_cloth_collision_solver_.can_solve_initial(views) ||
         !cloth_cloth_collision_solver_.can_build_body_triangle_ids(views))) {
        std::cerr << "Cannot initialize cloth-cloth contacts because required GPU resources are invalid.\n";
        return false;
    }

    for (const GarmentBufferRanges* garment_range : garment_ranges) {
        for (std::uint32_t iteration = 0; iteration < simulation_settings::prefit_iteration_count; ++iteration) {
            garment_prefit_solver_.solve(views.cloth_motion,
                                         *garment_range,
                                         views.body_triangle_geometry,
                                         views.body_triangle_bvh,
                                         gl);
        }
    }

    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);

    if (has_multiple_garments) {
        for (std::uint32_t iteration = 0; iteration < simulation_settings::solver_iteration_count; ++iteration) {
            if (!update_cloth_bvh_bounds(views, simulation_settings::cloth_collision_initial_detection_distance, gl)) {
                return false;
            }
            cloth_cloth_collision_detector_.detect(views, gl);
            cloth_cloth_collision_solver_.solve_initial(views, gl);
            gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
        }
    }

    if (has_multiple_garments && !cloth_cloth_collision_solver_.build_body_triangle_ids(views, gl)) {
        std::cerr << "Cannot cache cloth body triangle ids after garment pre-fit.\n";
        return false;
    }

    gpu_state.update_mesh_normals(gl);
    return true;
}

bool SimulationPipeline::step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    if (!initialized_) {
        return false;
    }

    const auto views = collect_gpu_views(gpu_state);
    if (!can_solve_constraint_iteration(views) || views.garment_buffer_ranges == nullptr) {
        std::cerr << "Cannot run simulation because required constraint or collision GPU resources are invalid.\n";
        return false;
    }

    const bool has_multiple_garments = scene.has_multiple_garments();

    if (has_multiple_garments &&
        !cloth_cloth_collision_solver_.build_body_triangle_ids(views, gl)) {
        std::cerr << "Cannot update cloth body triangle ids for simulation step.\n";
        return false;
    }

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    for (std::uint32_t substep = 0; substep < simulation_settings::substep_count; ++substep) {
        const std::int32_t start_boundary = static_cast<std::int32_t>(substep);
        const float previous_frame_time = character_frame_time(motion_step_index, start_boundary - 1);
        const float start_frame_time = character_frame_time(motion_step_index, start_boundary);
        const float end_frame_time = character_frame_time(motion_step_index, start_boundary + 1);
        const ReferenceFrameMotion pelvis_motion =
            sample_reference_motion(scene,
                                    GarmentCategory::Bottom,
                                    previous_frame_time,
                                    start_frame_time,
                                    end_frame_time,
                                    substep_dt_);
        const ReferenceFrameMotion torso_motion =
            sample_reference_motion(scene,
                                    GarmentCategory::Top,
                                    previous_frame_time,
                                    start_frame_time,
                                    end_frame_time,
                                    substep_dt_);

        update_character_substep_frame(scene, gpu_state, end_frame_time, gl);

        for (const GarmentObject& garment : scene.garments()) {
            const GarmentBufferRanges* garment_range =
                find_garment_range(*views.garment_buffer_ranges, garment.id);
            if (garment_range == nullptr) {
                std::cerr << "Cannot apply external forces because garment GPU ranges are missing.\n";
                return false;
            }

            const ReferenceFrameMotion& frame_motion =
                garment.mesh.garment_category == GarmentCategory::Top ? torso_motion : pelvis_motion;
            external_force_solver_.solve(views.cloth_motion,
                                         views.cloth_collision_pushout,
                                         views.cloth_contact_motion,
                                         *garment_range,
                                         substep_dt_,
                                         inverse_substep_dt_,
                                         external_acceleration,
                                         simulation_settings::velocity_damping,
                                         frame_motion,
                                         simulation_settings::reference_frame_inertia_scale,
                                         gl);
        }

        cloth_body_collision_detector_.detect(views, gl);

        if (has_multiple_garments) {
            const bool bounds_updated =
                update_cloth_bvh_bounds(views,
                                        simulation_settings::cloth_collision_detection_distance,
                                        gl);
            if (!bounds_updated) {
                return false;
            }
            cloth_cloth_collision_detector_.detect(views, gl);
        }

        for (std::uint32_t iteration = 0; iteration < simulation_settings::solver_iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(views.cloth_motion, views.stretch_constraints, gl);
            bending_constraint_solver_.solve(views.cloth_motion, views.bending_constraints, gl);
            attachment_constraint_solver_.solve(views.cloth_motion, views.attachment_constraints, views.body_triangle_geometry, gl);
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
    return true;
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

SimulationGpuViews SimulationPipeline::collect_gpu_views(const SceneGpuState& gpu_state)
{
    SimulationGpuViews views;
    views.cloth_motion = gpu_state.cloth_gpu_state().motion_buffer_view();
    views.cloth_collision_pushout = gpu_state.cloth_gpu_state().collision_pushout_buffer_view();
    views.cloth_contact_motion = gpu_state.cloth_gpu_state().contact_motion_buffer_view();
    views.cloth_body_triangle_ids = gpu_state.cloth_gpu_state().body_triangle_id_buffer_view();
    views.cloth_topology = gpu_state.cloth_gpu_state().mesh_topology_resources();
    views.cloth_bvh = gpu_state.cloth_bvh_buffer_view();
    views.garment_buffer_ranges = &gpu_state.cloth_gpu_state().garment_buffer_ranges();
    views.body_topology = gpu_state.character_gpu_state().mesh_topology_resources();
    views.body_vertices = gpu_state.character_gpu_state().character_vertex_buffer_view();
    views.body_triangle_geometry = gpu_state.character_gpu_state().character_triangle_geometry_resources();
    views.body_triangle_bvh = gpu_state.character_gpu_state().body_triangle_bvh_resources();
    views.body_vertex_bvh = gpu_state.character_gpu_state().body_vertex_bvh_resources();
    views.body_edge_bvh = gpu_state.character_gpu_state().body_edge_bvh_resources();
    views.collision_candidates = gpu_state.collision_candidate_buffer_view();
    views.stretch_constraints = gpu_state.cloth_gpu_state().stretch_constraint_buffer_view();
    views.bending_constraints = gpu_state.cloth_gpu_state().bending_constraint_buffer_view();
    views.attachment_constraints = gpu_state.cloth_gpu_state().attachment_constraint_buffer_view();
    return views;
}

bool SimulationPipeline::update_cloth_bvh_bounds(const SimulationGpuViews& views,
                                                 float bounds_margin,
                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    if (views.garment_buffer_ranges == nullptr) {
        std::cerr << "Cannot update cloth BVH bounds because garment buffer ranges are missing.\n";
        return false;
    }

    return cloth_bvh_bounds_updater_.update(views.cloth_motion,
                                            views.cloth_bvh,
                                            *views.garment_buffer_ranges,
                                            bounds_margin,
                                            gl);
}

bool SimulationPipeline::can_solve_constraint_iteration(const SimulationGpuViews& views) const
{
    return stretch_constraint_solver_.can_solve(views.cloth_motion, views.stretch_constraints) &&
           bending_constraint_solver_.can_solve(views.cloth_motion, views.bending_constraints) &&
           (views.attachment_constraints.constraint_count == 0 ||
            attachment_constraint_solver_.can_solve(views.cloth_motion, views.attachment_constraints, views.body_triangle_geometry)) &&
            cloth_body_collision_detector_.can_detect(views) &&
            cloth_body_collision_solver_.can_solve(views) &&
            cloth_cloth_collision_detector_.can_detect(views) &&
            cloth_cloth_collision_solver_.can_solve(views) &&
            ground_collision_solver_.can_solve(views.cloth_motion,
                                               views.cloth_collision_pushout,
                                               views.cloth_contact_motion);
}
