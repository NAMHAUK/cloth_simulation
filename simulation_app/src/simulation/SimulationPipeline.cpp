#include "simulation/SimulationPipeline.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"

#include <cassert>
#include <stdexcept>

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
      cloth_body_collision_detector_(params.collisions.body.detection_distance),
      cloth_body_collision_solver_(params.collisions.body),
      cloth_cloth_collision_detector_(params.collisions.cloth),
      cloth_cloth_collision_solver_(params.collisions.cloth),
      garment_prefit_solver_(params.prefit)
{}

// Initialization
void SimulationPipeline::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_valid_simulation_params(params_)) {
        throw std::runtime_error("Cannot initialize simulation pipeline with invalid parameters.");
    }

    substep_dt_ = params_.step.dt() / static_cast<float>(params_.step.substep_count);

    external_force_solver_.initialize(shader_dir, substep_dt_, gl);
    stretch_constraint_solver_.initialize(shader_dir, gl);
    bending_constraint_solver_.initialize(shader_dir, gl);
    attachment_constraint_solver_.initialize(shader_dir, gl);
    ground_collision_solver_.initialize(shader_dir, gl);
    cloth_body_collision_detector_.initialize(shader_dir, gl);
    cloth_body_collision_solver_.initialize(shader_dir, gl);
    cloth_cloth_collision_detector_.initialize(shader_dir, gl);
    cloth_cloth_collision_solver_.initialize(shader_dir, gl);
    garment_prefit_solver_.initialize(shader_dir, gl);

    initialized_ = true;
}

// Simulation
void SimulationPipeline::prefit_garments(SceneGpuState& gpu_state,
                                         const std::vector<const GarmentObject*>& garments,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    assert(!garments.empty());

    const auto views = gpu_state.simulation_view();

    // Garment pre-fit
    for (const GarmentObject* garment : garments) {
        for (std::uint32_t iteration = 0; iteration < params_.prefit.iteration_count; ++iteration) {
            garment_prefit_solver_.solve(views, garment->layer, gl);
        }
    }
    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);

    // Initial cloth-cloth collision
    if (views.has_multiple_garments()) {
        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            cloth_cloth_collision_detector_.detect_initial(views, gl);
            cloth_cloth_collision_solver_.solve_initial(views, gl);
            gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
        }
        cloth_cloth_collision_solver_.update_body_surface_mapping(views, gl);
    }

    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::step(SceneState& scene,
                              SceneGpuState& gpu_state,
                              std::uint32_t motion_step_index,
                              QOpenGLFunctions_4_5_Core& gl)
{
    const auto views = gpu_state.simulation_view();
    cloth_cloth_collision_solver_.update_body_surface_mapping(views, gl);

    const glm::vec3 external_acceleration = force_field_.external_acceleration();
    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        const float motion_frame_position =
            params_.step.motion_frame_position(motion_step_index, substep + 1u);
        const float frame_alpha = scene.motion_frame_alpha(motion_frame_position);
        scene.update_reference_frame_kinematics(frame_alpha, substep_dt_);
        gpu_state.update_character_pose(scene, frame_alpha, gl);

        solve_external_forces(scene, views, external_acceleration, gl);

        cloth_body_collision_detector_.detect(views, gl);
        cloth_cloth_collision_detector_.detect(views, gl);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(views, gl);
            bending_constraint_solver_.solve(views, gl);
            attachment_constraint_solver_.solve(views, gl);
            cloth_body_collision_solver_.solve(views, gl);
            cloth_cloth_collision_solver_.solve(views, gl);
            ground_collision_solver_.solve(views, gl);
        }
    }

    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::step_character_only(const SceneState& scene,
                                             SceneGpuState& gpu_state,
                                             std::uint32_t motion_step_index,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const float motion_frame_position = params_.step.motion_frame_position(motion_step_index + 1u, 0);
    const float frame_alpha = scene.motion_frame_alpha(motion_frame_position);
    gpu_state.update_character_pose(scene, frame_alpha, gl);
}

void SimulationPipeline::solve_external_forces(const SceneState& scene,
                                               const SimulationGpuView& views,
                                               const glm::vec3& external_acceleration,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    for (const GarmentObject& garment : scene.garments()) {
        const GarmentBufferState& garment_state = views.garment_buffer_states[garment.layer];
        assert(garment_state.vertex_count != 0u);

        const Kinematics& reference_frame_kinematics =
            scene.reference_frame_kinematics(garment.mesh.garment_category);
        external_force_solver_.solve(views,
                                     garment.layer,
                                     external_acceleration,
                                     reference_frame_kinematics,
                                     gl);
    }
}

// Accessors
bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

// Release
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
    substep_dt_ = 0.0f;
    initialized_ = false;
}
