#include "simulation/pd/PdSimulation.h"
#include "MeshNormals.h"
#include "gpu/common/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include <cuda_gl_interop.h>

// Initialization
PdSimulation::PdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params)
    : scene_(scene),
      gpu_state_(gpu_state),
      params_(params),
      dt_(params.step.dt()),
      projective_dynamics_solver_(pd_gpu_state_,
                                  params.integration.gravity,
                                  dt_,
                                  params.step.iteration_count,
                                  params.constraints.attachment_surface_offset,
                                  params.collisions.ground.height)
{}

void PdSimulation::initialize(const std::filesystem::path&, QOpenGLFunctions_4_5_Core& gl)
{
    initialize_cuda();
    pd_gpu_state_.initialize(dt_);
    pd_gpu_state_.character_gpu_state().initialize(scene_, gpu_state_.character_gpu_state());
    set_character_motion(gl);

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
    pd_gpu_state_.release_garment_resources();
    gpu_state_.cloth_gpu_state().rebuild_buffers(scene_.garments(), changed_layer, gl);
    pd_gpu_state_.cloth_gpu_state().prepare_normal_buffers(scene_.garments(), gpu_state_.cloth_gpu_state());
}

void PdSimulation::create_simulation_buffers(QOpenGLFunctions_4_5_Core&)
{
    pd_gpu_state_.release_simulation_buffers();
    pd_gpu_state_.prepare_system(scene_.garments(), gpu_state_.cloth_gpu_state());

    PdClothGpuState& cloth_state = pd_gpu_state_.cloth_gpu_state();
    if (cloth_state.vertex_count() == 0) {
        return;
    }

    const MappedMeshBuffers cloth_buffers = cloth_state.shared_buffers.map();
    const MappedMeshBuffers character_buffers = pd_gpu_state_.character_gpu_state().shared_buffers.map();

    update_normals(cloth_buffers, cloth_state.vertex_count(), cloth_state.triangle_color_counts());
    find_attachment_targets(pd_gpu_state_.constraints().get<ConstraintIndex::Attachment>(),
                            cloth_buffers.positions,
                            character_buffers,
                            gpu_state_.character_gpu_state().triangle_count());

    pd_gpu_state_.character_gpu_state().shared_buffers.unmap();
    cloth_state.shared_buffers.unmap();
}

// Character
void PdSimulation::set_character_motion(QOpenGLFunctions_4_5_Core& gl)
{
    pd_gpu_state_.character_gpu_state().set_motion(scene_.character_motion());
    update_character_pose(0.0f, gl);
}

void PdSimulation::update_character_motion(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl)
{
    const float motion_frame_position = (static_cast<float>(motion_step_index) + 1.0f) / params_.step.motion_stride();
    const float frame_alpha = scene_.motion_frame_alpha(motion_frame_position);
    scene_.update_reference_frame_kinematics(frame_alpha, dt_);
    update_character_pose(frame_alpha, gl);
}

void PdSimulation::update_character_pose(float frame_alpha, QOpenGLFunctions_4_5_Core&)
{
    PdCharacterGpuState& character_state = pd_gpu_state_.character_gpu_state();
    const std::uint32_t frame_index = std::min(scene_.motion_frame_index() + (frame_alpha >= 1.0f ? 1u : 0u),
                                               scene_.character_motion().frame_count - 1u);

    const MappedMeshBuffers character_buffers = character_state.shared_buffers.map();
    character_state.update_positions(character_buffers.positions, frame_index);

    update_normals(character_buffers,
                   character_state.vertex_count(),
                   gpu_state_.character_gpu_state().triangle_color_counts());
    character_state.shared_buffers.unmap();
}

// Base positions
void PdSimulation::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!gpu_state_.cloth_gpu_state().is_initialized()){
        return;
    }
    gpu_state_.cloth_gpu_state().capture_base_positions(gl);
}

void PdSimulation::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    auto& cloth_state = gpu_state_.cloth_gpu_state();
    auto& pd_cloth_state = pd_gpu_state_.cloth_gpu_state();
    if (!cloth_state.is_initialized()) {
        return;
    }

    cloth_state.restore_base_positions(gl);
    pd_cloth_state.reset_velocities();

    const MappedMeshBuffers cloth_buffers = pd_cloth_state.shared_buffers.map();
    update_normals(cloth_buffers, pd_cloth_state.vertex_count(), pd_cloth_state.triangle_color_counts());
    pd_cloth_state.shared_buffers.unmap();
}

void PdSimulation::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.cloth_gpu_state().clear_base_positions(gl);
}

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
    pd_gpu_state_.release_garment_resources();
    gpu_state_.cloth_gpu_state().release(gl);
}
