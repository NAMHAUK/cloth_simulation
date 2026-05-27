#include "simulation/SimulationController.h"

#include "simulation/SimulationSettings.h"

#include <iostream>
#include <utility>

#include <QObject>

SimulationController::SimulationController()
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

void SimulationController::set_viewport_callbacks(ViewportCallbacks callbacks)
{
    viewport_callbacks_ = std::move(callbacks);
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

    gpu_state_.set_garment_meshes(scene_, gl);
    gpu_state_.sync(scene_, gl);

    gpu_released_ = false;
    if (!frame_timer_.isActive()) {
        frame_timer_.start(simulation_settings::simulation_tick_ms);
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
    scene_.update_character_frame(simulation_step_count_, simulation_settings::character_frame_stride);

    if (!is_viewport_ready() || !is_gpu_initialized()) {
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_step(gl);
        sync_gpu(gl);
    });

    ++simulation_step_count_;
    viewport_callbacks_.request_redraw();
}

void SimulationController::release_gpu()
{
    if (gpu_released_ || !is_viewport_ready()) {
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
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
    if (!is_viewport_ready()) {
        std::cerr << "Cannot set character mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        scene_.set_character_mesh(std::move(mesh));
        gpu_state_.set_character_mesh(scene_, gl);
    });

    simulation_step_count_ = 0;
    if (scene_.has_character()) {
        viewport_callbacks_.reset_camera_to_character(scene_.character_mesh());
    }

    viewport_callbacks_.request_redraw();
}

void SimulationController::add_garment_mesh(GarmentMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        scene_.add_garment_mesh(std::move(mesh));
        gpu_state_.set_garment_meshes(scene_, gl);
    });

    viewport_callbacks_.request_redraw();
}

void SimulationController::set_playing(bool playing)
{
    scene_.set_playing(playing);
}

bool SimulationController::is_viewport_ready() const
{
    return viewport_callbacks_.is_ready &&
           viewport_callbacks_.run_with_gl_context &&
           viewport_callbacks_.request_redraw &&
           viewport_callbacks_.reset_camera_to_character &&
           viewport_callbacks_.is_ready();
}

bool SimulationController::simulation_step(QOpenGLFunctions_4_5_Core& gl)
{
    return cloth_pipeline_.step(scene_, gpu_state_, gl);
}

void SimulationController::sync_gpu(QOpenGLFunctions_4_5_Core& gl)
{
    gpu_state_.sync(scene_, gl);
}
