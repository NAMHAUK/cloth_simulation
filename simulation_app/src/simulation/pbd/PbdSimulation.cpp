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

    buffer_bindings_.initialize(gl);
    pbd_gpu_state_.initialize(shader_dir,
                              params_.constraints.attachment_surface_offset,
                              params_.collisions.body.detection_distance,
                              scene_,
                              gpu_state_.character_gpu_state(),
                              gl);
    buffer_bindings_.bind_character(gpu_state_.character_gpu_state().buffer_set(),
                                    pbd_gpu_state_.character_gpu_state().buffer_set(),
                                    gl);
    set_character_motion(gl);

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
    const PbdClothGpuState& pbd_cloth_state = pbd_gpu_state_.cloth_gpu_state();

    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        update_character_motion(motion_step_index, substep + 1u, gl);

        cloth_integrator_.integrate(cloth_state, scene_, gl);

        pbd_gpu_state_.update_cloth_bvh_bounds(params_.collisions.cloth.detection_distance, gl);
        collision_detector_.detect(cloth_state, pbd_gpu_state_, gl);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(pbd_cloth_state, gl);
            bending_constraint_solver_.solve(pbd_cloth_state, gl);
            attachment_constraint_solver_.solve(pbd_cloth_state, gl);
            cloth_body_collision_solver_.solve(cloth_state, pbd_gpu_state_, gl);
            cloth_cloth_collision_solver_.solve(cloth_state, pbd_gpu_state_.collision_buffers(), gl);
            ground_collision_solver_.solve(cloth_state, gl);
        }
    }

    pbd_gpu_state_.update_cloth_normals(cloth_state.element_counts(), gl);
}

void PbdSimulation::prepare_garments(const std::vector<const GarmentObject*>& placement_garments,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    auto& pbd_cloth_state = pbd_gpu_state_.cloth_gpu_state();

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
            pbd_gpu_state_.update_cloth_bvh_bounds(params_.collisions.cloth.initial_detection_distance, gl);
            collision_detector_.detect_prefit(cloth_state, pbd_gpu_state_, gl);
            cloth_cloth_collision_solver_.solve_initial(cloth_state, pbd_gpu_state_.collision_buffers(), gl);
        }
    }

    pbd_cloth_state.reset_velocity(cloth_state, 0, cloth_state.element_counts().vertex, gl);
    pbd_gpu_state_.update_cloth_normals(cloth_state.element_counts(), gl);

    // Attachment setup
    for (const GarmentObject* garment : placement_garments) {
        pbd_cloth_state.upload_attachment_indices(*garment, cloth_state, gl);
        pbd_gpu_state_.build_attachment_targets(garment->layer, gl);
        pbd_cloth_state.activate_attachment_targets(garment->layer);
    }
}

// Garment resources
void PbdSimulation::rebuild_garment_resources(GarmentLayer changed_layer, QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    auto& pbd_cloth_state = pbd_gpu_state_.cloth_gpu_state();
    pbd_cloth_state.release_distance_constraint_buffers(gl);
    pbd_cloth_state.release_collision_buffers(gl);
    release_collision_buffers(gl);

    const auto triangle_vertex_indices = cloth_state.rebuild_buffers(scene_.garments(), changed_layer, gl);
    pbd_cloth_state.rebuild_normal_buffers(triangle_vertex_indices, cloth_state.element_counts().vertex, gl);
    buffer_bindings_.bind_cloth(cloth_state.buffer_set(), pbd_cloth_state.buffer_set(), gl);
    pbd_gpu_state_.update_cloth_normals(cloth_state.element_counts(), gl);
}

void PbdSimulation::create_simulation_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    auto& pbd_cloth_state = pbd_gpu_state_.cloth_gpu_state();
    const auto triangle_vertex_indices = cloth_state.upload_triangle_indices(scene_.garments(), gl);
    pbd_cloth_state.rebuild_normal_buffers(triangle_vertex_indices, cloth_state.element_counts().vertex, gl);
    pbd_cloth_state.create_collision_buffers(scene_.garments(), cloth_state, gl);
    pbd_cloth_state.create_distance_constraint_buffers(scene_.garments(), cloth_state, gl);

    release_collision_buffers(gl);
    pbd_gpu_state_.create_collision_buffers(cloth_state.element_counts().vertex, gl);
    buffer_bindings_.bind_collision(pbd_gpu_state_.collision_buffers(), gl);

    if (!pbd_cloth_state.has_gpu_objects()) {
        throw std::runtime_error("Cannot prepare garment simulation: a cloth GPU object is missing.");
    }

    buffer_bindings_.bind_cloth(cloth_state.buffer_set(), pbd_cloth_state.buffer_set(), gl);
    pbd_gpu_state_.update_cloth_normals(cloth_state.element_counts(), gl);
}

// Character
void PbdSimulation::set_character_motion(QOpenGLFunctions_4_5_Core& gl)
{
    pbd_gpu_state_.character_gpu_state().set_motion(scene_.character_motion(), gpu_state_.character_gpu_state(), gl);
    pbd_gpu_state_.update_body_bvh_bounds(gl);
    pbd_gpu_state_.update_character_vertex_normals(gpu_state_.character_gpu_state().vertex_count(), gl);
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
    pbd_gpu_state_.character_gpu_state().update_pose(scene_.motion_frame_index(),
                                                     frame_alpha,
                                                     gpu_state_.character_gpu_state(),
                                                     gl);
    pbd_gpu_state_.update_body_bvh_bounds(gl);
    pbd_gpu_state_.update_character_vertex_normals(gpu_state_.character_gpu_state().vertex_count(), gl);
}

// Base positions
void PbdSimulation::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!gpu_state_.cloth_gpu_state().is_initialized() || !pbd_gpu_state_.cloth_gpu_state().is_initialized()) {
        return;
    }
    gpu_state_.cloth_gpu_state().capture_base_positions(gl);
}

void PbdSimulation::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    if (!cloth_state.is_initialized() || !pbd_gpu_state_.cloth_gpu_state().is_initialized()) {
        return;
    }
    cloth_state.restore_base_positions(gl);
    pbd_gpu_state_.cloth_gpu_state().reset_velocity(cloth_state, 0, cloth_state.element_counts().vertex, gl);
    pbd_gpu_state_.update_cloth_normals(cloth_state.element_counts(), gl);
}

void PbdSimulation::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.cloth_gpu_state().clear_base_positions(gl);
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

    buffer_bindings_.reset_cloth_bindings(gl);
    buffer_bindings_.reset_collision_bindings(gl);
    buffer_bindings_.reset_character_bindings(gl);
    pbd_gpu_state_.release(gl);
    buffer_bindings_.release(gl);

    substep_dt_ = 0.0f;
    initialized_ = false;
}

void PbdSimulation::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.reset_cloth_bindings(gl);
    release_collision_buffers(gl);
    pbd_gpu_state_.cloth_gpu_state().release(gl);
    gpu_state_.cloth_gpu_state().release(gl);
}

void PbdSimulation::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.reset_collision_bindings(gl);
    pbd_gpu_state_.release_collision_buffers(gl);
}
