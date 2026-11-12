#include "simulation/pbd/PbdSimulation.h"
#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <stdexcept>

namespace {
std::uint32_t calculate_iteration_count(const std::vector<GarmentObject>& garments)
{
    std::uint32_t iteration_count = 8u;
    for (const GarmentObject& garment : garments) {
        const auto& rest_lengths = garment.mesh.stretch_constraints.rest_lengths;
        const double total_rest_length = std::accumulate(rest_lengths.begin(), rest_lengths.end(), 0.0);
        const double average_rest_length = total_rest_length / rest_lengths.size();

        std::uint32_t garment_iteration_count = 8u;
        if (average_rest_length <= 0.005f) {
            garment_iteration_count = 16u;
        } else if (average_rest_length < 0.007f) {
            garment_iteration_count = 14u;
        } else if (average_rest_length < 0.009f) {
            garment_iteration_count = 12u;
        } else if (average_rest_length < 0.013f) {
            garment_iteration_count = 10u;
        }
        iteration_count = std::max(iteration_count, garment_iteration_count);
    }
    return iteration_count;
}
}

// Initialization
PbdSimulation::PbdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params)
    : scene_(scene),
      gpu_state_(gpu_state),
      params_(params),
      cloth_integrator_(params.integration),
      stretch_constraint_solver_(params.constraints.stretch_stiffness),
      bending_constraint_solver_(params.constraints.bending_stiffness),
      attachment_constraint_solver_(params.constraints.attachment_stiffness),
      ground_collision_solver_(params.collisions.ground),
      collision_detector_(params.collisions.cloth),
      cloth_body_collision_solver_(params.collisions.body),
      cloth_cloth_collision_solver_(params.collisions.cloth),
      garment_prefit_solver_(params.prefit)
{}

void PbdSimulation::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_valid_simulation_params(params_)) {
        throw std::runtime_error("Cannot initialize simulation pipeline with invalid parameters.");
    }

    substep_dt_ = params_.step.dt() / static_cast<float>(params_.step.substep_count);

    cloth_integrator_.initialize(shader_dir, substep_dt_, gl);
    stretch_constraint_solver_.initialize(shader_dir, gl);
    bending_constraint_solver_.initialize(shader_dir, gl);
    attachment_constraint_solver_.initialize(shader_dir, gl);
    ground_collision_solver_.initialize(shader_dir, gl);
    collision_detector_.initialize(shader_dir, gl);
    cloth_body_collision_solver_.initialize(shader_dir, gl);
    cloth_cloth_collision_solver_.initialize(shader_dir, gl);
    garment_prefit_solver_.initialize(shader_dir, gl);

    initialized_ = true;
}

// Simulation
void PbdSimulation::step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());
    const ClothGpuState& cloth_state = gpu_state_.cloth_gpu_state();
    const PbdClothGpuState& pbd_cloth_state = gpu_state_.pbd_gpu_state().cloth_gpu_state();

    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        update_character_motion(motion_step_index, substep + 1u, gl);

        cloth_integrator_.integrate(cloth_state, scene_, gl);

        gpu_state_.update_cloth_bvh_bounds(params_.collisions.cloth.detection_distance, gl);
        collision_detector_.detect(gpu_state_, gl);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(pbd_cloth_state, gl);
            bending_constraint_solver_.solve(pbd_cloth_state, gl);
            attachment_constraint_solver_.solve(pbd_cloth_state, gl);
            cloth_body_collision_solver_.solve(gpu_state_, gl);
            cloth_cloth_collision_solver_.solve(gpu_state_, gl);
            ground_collision_solver_.solve(cloth_state, gl);
        }
    }

    gpu_state_.update_cloth_normals(gl);
}

void PbdSimulation::prepare_garments(const std::vector<const GarmentObject*>& placement_garments,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    auto& pbd_cloth_state = gpu_state_.pbd_gpu_state().cloth_gpu_state();

    // Buffer preparation
    pbd_cloth_state.rebuild_dynamic_buffers(cloth_state, gl);
    pbd_cloth_state.rebuild_attachment_buffers(scene_.garments(), placement_garments, gl);
    create_simulation_buffers(gl);

    // Garment pre-fit
    params_.step.iteration_count = calculate_iteration_count(scene_.garments());

    for (std::uint32_t step = 0; step < params_.prefit.iteration_count; ++step) {
        pbd_cloth_state.reset_velocity(cloth_state, 0, cloth_state.element_counts().vertex, gl);
        garment_prefit_solver_.solve(cloth_state, gl);
        stretch_constraint_solver_.solve(pbd_cloth_state, gl);
        bending_constraint_solver_.solve(pbd_cloth_state, gl);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            gpu_state_.update_cloth_bvh_bounds(params_.collisions.cloth.initial_detection_distance, gl);
            collision_detector_.detect_prefit(gpu_state_, gl);
            cloth_cloth_collision_solver_.solve_initial(gpu_state_, gl);
        }
    }

    pbd_cloth_state.reset_velocity(cloth_state, 0, cloth_state.element_counts().vertex, gl);
    gpu_state_.update_cloth_normals(gl);

    // Attachment setup
    for (const GarmentObject* garment : placement_garments) {
        gpu_state_.initialize_garment_attachments(*garment, gl);
    }
}

// Garment resources
void PbdSimulation::rebuild_garment_resources(GarmentLayer changed_layer, QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.rebuild_garment_resources(scene_, gl, changed_layer);
}

void PbdSimulation::create_simulation_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.create_simulation_buffers(scene_, gl);
}

// Character
void PbdSimulation::set_character_motion(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.set_character_motion(scene_, gl);
}

void PbdSimulation::update_character_motion(std::uint32_t motion_step_index,
                                            std::uint32_t substep,
                                            QOpenGLFunctions_4_5_Core& gl)
{
    const float motion_frame_position = params_.step.motion_frame_position(motion_step_index, substep);
    const float frame_alpha = scene_.motion_frame_alpha(motion_frame_position);
    scene_.update_reference_frame_kinematics(frame_alpha, substep_dt_);
    update_character_pose(frame_alpha, gl);
}

void PbdSimulation::update_character_pose(float frame_alpha, QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.update_character_pose(scene_, frame_alpha, gl);
}

// Base positions
void PbdSimulation::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.capture_garment_base_positions(gl);
}

void PbdSimulation::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.restore_garment_base_positions(gl);
}

void PbdSimulation::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.clear_garment_base_positions(gl);
}

// State
bool PbdSimulation::is_initialized() const
{
    return initialized_;
}

// Release
void PbdSimulation::release(QOpenGLFunctions_4_5_Core& gl)
{
    garment_prefit_solver_.release(gl);
    cloth_cloth_collision_solver_.release(gl);
    cloth_body_collision_solver_.release(gl);
    collision_detector_.release(gl);
    ground_collision_solver_.release(gl);
    attachment_constraint_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    cloth_integrator_.release(gl);

    substep_dt_ = 0.0f;
    initialized_ = false;
}

void PbdSimulation::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.release_garment_resources(gl);
}
