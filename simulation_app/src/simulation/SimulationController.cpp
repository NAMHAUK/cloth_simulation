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

    frame_timer_.start(simulation_settings::simulation_tick_ms);
    return true;
}

// rendering //

void SimulationController::draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl)
{
    render_pipeline_.draw(scene_, gpu_state_, mvp, gl);
}

void SimulationController::tick_frame()
{
    if (!is_viewport_ready() || !is_gpu_initialized()) {
        return;
    }

    bool simulation_step_finished = false;
    if (simulation_running_ || has_garment_placement_update()) {
        viewport_callbacks_.run_with_gl_context([this, &simulation_step_finished](QOpenGLFunctions_4_5_Core& gl) {
            set_current_garment_placement(gl);

            if (simulation_running_) {
                simulation_step_finished = simulation_pipeline_.step(scene_, gpu_state_, motion_step_count_, gl);
            }
        });
    }

    if (simulation_step_finished) {
        ++motion_step_count_;
    }

    viewport_callbacks_.request_update();
}

// Object //

void SimulationController::load_default_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    default_character_mesh_ = std::move(mesh);
    set_character_mesh_state(default_character_mesh_, gl);
    is_default_pose_ = true;
}

void SimulationController::set_character_mesh(CharacterMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot set character mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        set_character_mesh_state(std::move(mesh), gl);
    });

    viewport_callbacks_.request_update();
}

void SimulationController::set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_mesh(std::move(mesh));
    gpu_state_.set_character_mesh(scene_, gl);
    motion_step_count_ = 0;
    is_default_pose_ = false;
    viewport_callbacks_.reset_camera_to_character(scene_.character_mesh());
}

void SimulationController::add_garment_mesh(GarmentMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        // 배치중이던 garment 있으면 제거
        if (garment_placement_.garment_id != 0 && scene_.remove_garment(garment_placement_.garment_id)) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }

        // 새 garment 추가
        garment_placement_.clear();
        garment_placement_.garment_id = scene_.add_garment_mesh(std::move(mesh));
        gpu_state_.update_garment_meshes(scene_, gl);
    });

    viewport_callbacks_.request_update();
}

void SimulationController::set_current_garment_placement(QOpenGLFunctions_4_5_Core& gl)
{
    if (!has_garment_placement_update()) {
        return;
    }

    const bool update_rest_lengths = garment_placement_.scale_changed;
    const GarmentObject* garment = scene_.update_garment_placement(garment_placement_.garment_id,
                                                                   garment_placement_.position_offset,
                                                                   garment_placement_.scale);
    if (garment != nullptr) {
        gpu_state_.update_garment_placement(*garment, update_rest_lengths, gl);
    }

    garment_placement_.clear_update();
}

void SimulationController::reset_scene_to_default()
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot reset scene before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_running_ = false;
        motion_step_count_ = 0;
        garment_placement_.clear();

        scene_.clear_garments();
        gpu_state_.update_garment_meshes(scene_, gl);
        set_character_mesh_state(default_character_mesh_, gl);
        is_default_pose_ = true;
    });

    viewport_callbacks_.request_update();
}

// garment placement panel //
void SimulationController::set_garment_placement(const glm::vec3& position_offset, float scale)
{
    if (garment_placement_.garment_id == 0 || scale <= 0.0f) {
        return;
    }

    if (garment_placement_.position_offset != position_offset) {
        garment_placement_.position_offset = position_offset;
        garment_placement_.position_changed = true;
    }
    if (garment_placement_.scale != scale) {
        garment_placement_.scale = scale;
        garment_placement_.scale_changed = true;
    }
}

void SimulationController::confirm_garment_placement()
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot confirm garment placement before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        set_current_garment_placement(gl);
        garment_placement_.clear();
    });

    viewport_callbacks_.request_update();
}

void SimulationController::cancel_garment_placement()
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot cancel garment placement before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        if (garment_placement_.garment_id != 0 && scene_.remove_garment(garment_placement_.garment_id)) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }

        garment_placement_.clear();
    });

    viewport_callbacks_.request_update();
}

// getter //
bool SimulationController::is_viewport_ready() const
{
    return viewport_callbacks_.is_ready &&
           viewport_callbacks_.run_with_gl_context &&
           viewport_callbacks_.request_update &&
           viewport_callbacks_.reset_camera_to_character &&
           viewport_callbacks_.is_ready();
}

bool SimulationController::is_gpu_initialized() const
{
    return gpu_state_.is_initialized() &&
           simulation_pipeline_.is_initialized() &&
           render_pipeline_.is_initialized();
}

bool SimulationController::is_simulation_running() const
{
    return simulation_running_;
}

bool SimulationController::is_default_pose() const
{
    return is_default_pose_;
}

bool SimulationController::has_garment_placement_update() const
{
    return garment_placement_.has_update();
}

// setter //
void SimulationController::start_simulation()
{
    simulation_running_ = true;
}

void SimulationController::stop_simulation()
{
    simulation_running_ = false;
}

void SimulationController::set_viewport_callbacks(ViewportCallbacks callbacks)
{
    viewport_callbacks_ = std::move(callbacks);
}

void SimulationController::release_gpu()
{
    if (!is_viewport_ready() || !is_gpu_initialized()) {
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        render_pipeline_.release(gl);
        simulation_pipeline_.release(gl);
        gpu_state_.release(gl);
    });
}
