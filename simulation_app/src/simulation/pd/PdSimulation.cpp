#include "simulation/pd/PdSimulation.h"
#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"
#include "utils/CudaUtils.h"

#include <cassert>
#include <stdexcept>

// Initialization
PdSimulation::PdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params)
    : scene_(scene),
      gpu_state_(gpu_state),
      params_(params),
      projective_dynamics_solver_(pd_gpu_state_, params.integration.gravity)
{}

void PdSimulation::initialize(const std::filesystem::path&, QOpenGLFunctions_4_5_Core&)
{
    if (!is_valid_simulation_params(params_)) {
        throw std::runtime_error("Cannot initialize simulation pipeline with invalid parameters.");
    }

    substep_dt_ = params_.step.dt() / static_cast<float>(params_.step.substep_count);
    initialize_cuda();
    projective_dynamics_solver_.initialize(substep_dt_);
    initialized_ = true;
}

// Simulation
void PdSimulation::step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());

    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        update_character_motion(motion_step_index, substep + 1u, gl);
        projective_dynamics_solver_.solve();
    }
}

void PdSimulation::prepare_garments(const std::vector<const GarmentObject*>&, QOpenGLFunctions_4_5_Core& gl)
{
    create_simulation_buffers(gl);

    // Garment pre-fit
    assert(is_initialized());
}

// Garment resources
void PdSimulation::rebuild_garment_resources(GarmentLayer changed_layer, QOpenGLFunctions_4_5_Core& gl)
{
    pd_gpu_state_.release();
    gpu_state_.cloth_gpu_state().rebuild_buffers(scene_.garments(), changed_layer, gl);
}

void PdSimulation::create_simulation_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.cloth_gpu_state().upload_triangle_indices(scene_.garments(), gl);
    pd_gpu_state_.rebuild_buffers(gpu_state_.cloth_gpu_state());
}

// Character
void PdSimulation::set_character_motion(QOpenGLFunctions_4_5_Core&)
{}

void PdSimulation::update_character_motion(std::uint32_t motion_step_index,
                                           std::uint32_t substep,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    const float motion_frame_position = params_.step.motion_frame_position(motion_step_index, substep);
    const float frame_alpha = scene_.motion_frame_alpha(motion_frame_position);
    scene_.update_reference_frame_kinematics(frame_alpha, substep_dt_);
    update_character_pose(frame_alpha, gl);
}

void PdSimulation::update_character_pose(float, QOpenGLFunctions_4_5_Core&)
{}

// Base positions
void PdSimulation::capture_garment_base_positions(QOpenGLFunctions_4_5_Core&)
{}

void PdSimulation::restore_garment_base_positions(QOpenGLFunctions_4_5_Core&)
{
    if (!gpu_state_.cloth_gpu_state().is_initialized()) {
        return;
    }
    pd_gpu_state_.cloth_gpu_state().reset_velocities();
}

void PdSimulation::clear_garment_base_positions(QOpenGLFunctions_4_5_Core&)
{}

// State
bool PdSimulation::is_initialized() const
{
    return initialized_;
}

// Release
void PdSimulation::release(QOpenGLFunctions_4_5_Core&)
{
    projective_dynamics_solver_.release();
    pd_gpu_state_.release();
    substep_dt_ = 0.0f;
    initialized_ = false;
}

void PdSimulation::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    pd_gpu_state_.release();
    gpu_state_.cloth_gpu_state().release(gl);
}
