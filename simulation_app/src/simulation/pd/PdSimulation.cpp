#include "simulation/pd/PdSimulation.h"
#include "gpu/common/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <cassert>
#include <stdexcept>

#include <cuda_gl_interop.h>

// Initialization
PdSimulation::PdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params)
    : scene_(scene),
      gpu_state_(gpu_state),
      params_(params),
      dt_(params.step.dt()),
      projective_dynamics_solver_(pd_gpu_state_, params.integration.gravity, dt_)
{}

void PdSimulation::initialize(const std::filesystem::path&, QOpenGLFunctions_4_5_Core&)
{
    initialize_cuda();
    pd_gpu_state_.initialize(dt_);
    initialized_ = true;
}

void PdSimulation::initialize_cuda()
{
    unsigned int cuda_device_count = 0;
    int cuda_device = -1;
    if (cudaGLGetDevices(&cuda_device_count, &cuda_device, 1, cudaGLDeviceListAll) != cudaSuccess ||
        cuda_device_count == 0 ||
        cudaSetDevice(cuda_device) != cudaSuccess) {
        throw std::runtime_error("CUDA device initialization failed.");
    }
}

// Simulation
void PdSimulation::step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());

    update_character_motion(motion_step_index, gl);
    projective_dynamics_solver_.solve();
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
    pd_gpu_state_.prepare_system(scene_.garments(), gpu_state_.cloth_gpu_state());
}

// Character
void PdSimulation::set_character_motion(QOpenGLFunctions_4_5_Core&)
{}

void PdSimulation::update_character_motion(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    const float motion_frame_position = (static_cast<float>(motion_step_index) + 1.0f) / params_.step.motion_stride();
    const float frame_alpha = scene_.motion_frame_alpha(motion_frame_position);
    scene_.update_reference_frame_kinematics(frame_alpha, dt_);
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
    pd_gpu_state_.release();
    initialized_ = false;
}

void PdSimulation::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    pd_gpu_state_.release();
    gpu_state_.cloth_gpu_state().release(gl);
}
