#include "simulation/SimulationController.h"

#include "app/ProjectPaths.h"
#include "simulation/SimulationSettings.h"

#include <cassert>
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

bool SimulationController::initialize_gpu(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    assert(!is_gpu_initialized());
    if (is_gpu_initialized()) {
        return false;
    }

    if (!gpu_state_.initialize(shader_paths, gl) || !simulation_pipeline_.initialize(shader_paths, gl) || !render_pipeline_.initialize(shader_paths, gl)) {

        render_pipeline_.release(gl);
        simulation_pipeline_.release(gl);
        gpu_state_.release(gl);
        return false;
    }

    gpu_released_ = false;
    frame_timer_.start(simulation_settings::simulation_tick_ms);
    return true;
}

void SimulationController::draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    render_pipeline_.draw(scene_, gpu_state_, mvp, gl);
}

// frame마다 실행되는 함수
void SimulationController::tick_frame()
{
    if (!is_viewport_ready() || !is_gpu_initialized()) {
        return;
    }

    bool simulation_step_finished = false;
    viewport_callbacks_.run_with_gl_context([this, &simulation_step_finished](QOpenGLFunctions_4_5_Core& gl) {
        simulation_step_finished = simulation_pipeline_.step(scene_, gpu_state_, motion_step_count_, gl);
    });

    if (simulation_step_finished) {
        ++simulation_step_count_;
        ++motion_step_count_;
    }

    viewport_callbacks_.request_update();
}

void SimulationController::release_gpu()
{
    if (gpu_released_ || !is_viewport_ready()) {
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        render_pipeline_.release(gl);
        simulation_pipeline_.release(gl);
        gpu_state_.release(gl);
    });
    gpu_released_ = true;
}

bool SimulationController::is_gpu_initialized() const
{
    return gpu_state_.is_initialized() &&
           simulation_pipeline_.is_initialized() &&
           render_pipeline_.is_initialized();
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

    motion_step_count_ = 0;
    if (scene_.has_character()) {
        viewport_callbacks_.reset_camera_to_character(scene_.character_mesh());
    }

    viewport_callbacks_.request_update();
}

void SimulationController::add_garment_mesh(GarmentMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        scene_.add_garment_mesh(std::move(mesh));
        gpu_state_.update_garment_meshes(scene_, gl);
    });

    viewport_callbacks_.request_update();
}

bool SimulationController::is_viewport_ready() const
{
    return viewport_callbacks_.is_ready &&
           viewport_callbacks_.run_with_gl_context &&
           viewport_callbacks_.request_update &&
           viewport_callbacks_.reset_camera_to_character &&
           viewport_callbacks_.is_ready();
}
