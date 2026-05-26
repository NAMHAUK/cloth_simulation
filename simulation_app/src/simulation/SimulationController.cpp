#include "simulation/SimulationController.h"

#include "support/QtHelpers.h"
#include "ui/SceneViewport.h"

#include <iostream>
#include <utility>

#include <QObject>

namespace {
constexpr int playback_tick_ms = 16;
}

SimulationController::SimulationController(SceneViewport& viewport) : viewport_(viewport)
{
    // tick마다 frame update 함수 설정
    QObject::connect(&frame_timer_, &QTimer::timeout, &frame_timer_, [this]() {
        tick_frame();
    });
}

SimulationController::~SimulationController()
{
    release_gpu();
}

// GPU / rendering //

bool SimulationController::initialize_gpu(const std::filesystem::path& vertex_shader_path,
                                          const std::filesystem::path& fragment_shader_path,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    if (!gpu_state_.initialize(gl)) {
        return false;
    }

    if (!cloth_pipeline_.initialize(gl)) {
        gpu_state_.release(gl);
        return false;
    }

    if (!renderer_.initialize(vertex_shader_path, fragment_shader_path, gl)) {
        cloth_pipeline_.release(gl);
        gpu_state_.release(gl);
        return false;
    }

    for (const GarmentSceneObject& garment : scene_.garments()) {
        gpu_state_.add_garment_gpu_state(garment.id);
        gpu_state_.set_garment_mesh(garment, gl);
    }
    gpu_state_.sync(scene_, gl);

    gpu_released_ = false;
    if (!frame_timer_.isActive()) {
        playback_timer_.restart();
        frame_timer_.start(playback_tick_ms);
    }
    return true;
}

void SimulationController::draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    renderer_.draw(scene_, gpu_state_, mvp, gl);
}

// frame마다 실행되는 함수
void SimulationController::tick_frame()
{
    const double playback_seconds = static_cast<double>(playback_timer_.elapsed()) / 1000.0;
    scene_.update_playback_frame(playback_seconds);

    if (!viewport_.is_gl_initialized() || !is_gpu_initialized()) {
        return;
    }

    run_with_gl_context(viewport_, [&] {
        QOpenGLFunctions_4_5_Core& gl = viewport_.gl_functions();
        simulation_step(gl);
        sync_gpu(gl);
    });

    viewport_.update();
}

void SimulationController::release_gpu()
{
    if (gpu_released_ || !viewport_.is_gl_initialized()) {
        return;
    }

    run_with_gl_context(viewport_, [&] {
        QOpenGLFunctions_4_5_Core& gl = viewport_.gl_functions();
        renderer_.release(gl);
        cloth_pipeline_.release(gl);
        gpu_state_.release(gl);
    });
    gpu_released_ = true;
}

bool SimulationController::is_gpu_initialized() const
{
    return gpu_state_.is_initialized() &&
           cloth_pipeline_.is_initialized() &&
           renderer_.is_initialized();
}


// Scene editing //

void SimulationController::set_character_mesh(CharacterMesh mesh)
{
    if (!viewport_.is_gl_initialized()) {
        std::cerr << "Cannot set character mesh before OpenGL initialization.\n";
        return;
    }

    run_with_gl_context(viewport_, [&] {
        scene_.set_character_mesh(std::move(mesh));
        gpu_state_.set_character_mesh(scene_, viewport_.gl_functions());
    });

    playback_timer_.restart();
    if (scene_.has_character()) {
        viewport_.reset_camera_to_character(scene_.character_mesh());
    }

    viewport_.update();
}

void SimulationController::add_garment_mesh(GarmentMesh mesh)
{
    if (!viewport_.is_gl_initialized()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    run_with_gl_context(viewport_, [&] {
        const GarmentId garment_id = scene_.add_garment_mesh(std::move(mesh));
        gpu_state_.add_garment_gpu_state(garment_id);
        for (const GarmentSceneObject& garment : scene_.garments()) {
            if (garment.id == garment_id) {
                gpu_state_.set_garment_mesh(garment, viewport_.gl_functions());
                break;
            }
        }
    });

    viewport_.update();
}

void SimulationController::set_playing(bool playing)
{
    scene_.set_playing(playing);
}

bool SimulationController::simulation_step(QOpenGLFunctions_4_5_Core& gl)
{
    return cloth_pipeline_.step(scene_, gpu_state_, gl);
}

void SimulationController::sync_gpu(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.sync(scene_, gl);
}
