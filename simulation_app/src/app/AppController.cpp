#include "app/AppController.h"

#include "ui/SceneViewport.h"
#include "support/QtHelpers.h"

#include <iostream>
#include <utility>

#include <QObject>

namespace {
constexpr int playback_tick_ms = 16;
}

AppController::AppController(SceneViewport& viewport) : viewport_(viewport)
{
    // tick마다 frame update 함수 설정
    QObject::connect(&frame_timer_, &QTimer::timeout, &frame_timer_, [this]() {
        tick_frame();
    });
}

AppController::~AppController()
{
    release_gpu();
}

// GPU / rendering //

bool AppController::initialize_gpu(const std::filesystem::path& vertex_shader_path,
                                          const std::filesystem::path& fragment_shader_path,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    const bool initialized = runtime_.initialize_gpu(vertex_shader_path, fragment_shader_path, gl);
    if (initialized) {
        gpu_released_ = false;
        if (!frame_timer_.isActive()) {
            playback_timer_.restart();
            frame_timer_.start(playback_tick_ms);
        }
    }
    return initialized;
}

void AppController::draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    runtime_.draw(mvp, gl);
}

// frame마다 실행되는 함수
void AppController::tick_frame()
{
    const double playback_seconds = static_cast<double>(playback_timer_.elapsed()) / 1000.0;
    runtime_.update_playback_frame(playback_seconds);

    if (!viewport_.is_gl_initialized() || !runtime_.is_gpu_initialized()) {
        return;
    }

    run_with_gl_context(viewport_, [&] {
        QOpenGLFunctions_4_5_Core& gl = viewport_.gl_functions();
        runtime_.simulation_step(gl);
        runtime_.sync_gpu(gl);
    });

    viewport_.update();
}

void AppController::release_gpu()
{
    if (gpu_released_ || !viewport_.is_gl_initialized()) {
        return;
    }

    run_with_gl_context(viewport_, [&] {
        runtime_.release_gpu(viewport_.gl_functions());
    });
    gpu_released_ = true;
}

bool AppController::is_gpu_initialized() const
{
    return runtime_.is_gpu_initialized();
}


// Scene editing //

void AppController::set_character_mesh(CharacterMesh mesh)
{
    if (!viewport_.is_gl_initialized()) {
        std::cerr << "Cannot set character mesh before OpenGL initialization.\n";
        return;
    }

    run_with_gl_context(viewport_, [&] {
        runtime_.set_character_mesh(std::move(mesh), viewport_.gl_functions());
    });

    playback_timer_.restart();
    if (runtime_.scene().has_character()) {
        viewport_.reset_camera_to_character(runtime_.scene().character_mesh());
    }

    viewport_.update();
}

void AppController::add_garment_mesh(GarmentMesh mesh)
{
    if (!viewport_.is_gl_initialized()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    run_with_gl_context(viewport_, [&] {
        runtime_.add_garment_mesh(std::move(mesh), viewport_.gl_functions());
    });

    viewport_.update();
}
