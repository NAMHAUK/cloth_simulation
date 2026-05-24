#include "simulation/SimulationRuntime.h"

#include <utility>

const SimulationScene& SimulationRuntime::scene() const
{
    return scene_;
}

// Scene editing // 
void SimulationRuntime::set_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_mesh(std::move(mesh));
    gpu_state_.set_character_mesh(scene_, gl);
}

GarmentId SimulationRuntime::add_garment_mesh(GarmentMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentId garment_id = scene_.add_garment_mesh(std::move(mesh));
    gpu_state_.add_garment_gpu_state(garment_id);
    for (const GarmentSceneObject& garment : scene_.garments()) {
        if (garment.id == garment_id) {
            gpu_state_.set_garment_mesh(garment, gl);
            break;
        }
    }
    return garment_id;
}

// Playback / simulation //
bool SimulationRuntime::update_playback_frame(double playback_seconds)
{
    return scene_.update_playback_frame(playback_seconds);
}

bool SimulationRuntime::simulation_step(QOpenGLFunctions_4_5_Core&)
{
    return false;
}

void SimulationRuntime::set_playing(bool playing)
{
    scene_.set_playing(playing);
}

// GPU / rendering //
bool SimulationRuntime::initialize_gpu(const std::filesystem::path& vertex_shader_path,
                                       const std::filesystem::path& fragment_shader_path,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    if (!gpu_state_.initialize(gl)) {
        return false;
    }

    if (!renderer_.initialize(vertex_shader_path, fragment_shader_path, gl)) {
        gpu_state_.release(gl);
        return false;
    }

    for (const GarmentSceneObject& garment : scene_.garments()) {
        gpu_state_.add_garment_gpu_state(garment.id);
        gpu_state_.set_garment_mesh(garment, gl);
    }
    gpu_state_.sync(scene_, gl);
    return true;
}

bool SimulationRuntime::is_gpu_initialized() const
{
    return gpu_state_.is_initialized() && renderer_.is_initialized();
}

void SimulationRuntime::sync_gpu(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.sync(scene_, gl);
}

void SimulationRuntime::draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    renderer_.draw(scene_, gpu_state_, mvp, gl);
}

void SimulationRuntime::release_gpu(QOpenGLFunctions_4_5_Core& gl)
{
    renderer_.release(gl);
    gpu_state_.release(gl);
}
