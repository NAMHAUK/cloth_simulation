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
    if (simulation_running_ || has_pending_garment_placement()) {
        viewport_callbacks_.run_with_gl_context([this, &simulation_step_finished](QOpenGLFunctions_4_5_Core& gl) {
            apply_pending_garment_placement(gl);

            if (simulation_running_) {
                simulation_step_finished = simulation_pipeline_.step(scene_, gpu_state_, motion_step_count_, gl);
            }
        });
    }

    if (simulation_step_finished) {
        ++simulation_step_count_;
        ++motion_step_count_;
    }

    viewport_callbacks_.request_update();
}

// Scene editing //

void SimulationController::set_character_mesh(CharacterMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot set character mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        set_character_mesh_state(std::move(mesh), gl);
    });
}

void SimulationController::load_default_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    default_character_mesh_ = std::move(mesh);
    set_default_character_mesh(gl);
}

void SimulationController::set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_mesh(std::move(mesh));
    gpu_state_.set_character_mesh(scene_, gl);
    motion_step_count_ = 0;
    is_default_pose_ = false;
    viewport_callbacks_.reset_camera_to_character(scene_.character_mesh());

    viewport_callbacks_.request_update();
}

void SimulationController::set_default_character_mesh(QOpenGLFunctions_4_5_Core& gl)
{
    set_character_mesh_state(default_character_mesh_, gl);
    is_default_pose_ = true;
}

void SimulationController::add_garment_mesh(GarmentMesh mesh)
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot add garment mesh before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        editable_garment_id_ = scene_.add_garment_mesh(std::move(mesh));
        pending_position_offset_ = glm::vec3{0.0f};
        pending_scale_ = 1.0f;
        placement_position_changed_ = false;
        placement_scale_changed_ = false;
        gpu_state_.update_garment_meshes(scene_, gl);
    });

    viewport_callbacks_.request_update();
}

void SimulationController::reset_scene_to_default()
{
    if (!is_viewport_ready()) {
        std::cerr << "Cannot reset scene before OpenGL initialization.\n";
        return;
    }

    viewport_callbacks_.run_with_gl_context([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_running_ = false;
        simulation_step_count_ = 0;
        motion_step_count_ = 0;
        editable_garment_id_ = 0;
        pending_position_offset_ = glm::vec3{0.0f};
        pending_scale_ = 1.0f;
        placement_position_changed_ = false;
        placement_scale_changed_ = false;

        scene_.clear_garments();
        gpu_state_.update_garment_meshes(scene_, gl);
        set_default_character_mesh(gl);
    });

    viewport_callbacks_.request_update();
}

void SimulationController::set_garment_placement(const glm::vec3& position_offset, float scale)
{
    if (editable_garment_id_ == 0 || scale <= 0.0f) {
        return;
    }

    if (pending_position_offset_ != position_offset) {
        pending_position_offset_ = position_offset;
        placement_position_changed_ = true;
    }
    if (pending_scale_ != scale) {
        pending_scale_ = scale;
        placement_scale_changed_ = true;
    }
}

bool SimulationController::has_pending_garment_placement() const
{
    return editable_garment_id_ != 0 && (placement_position_changed_ || placement_scale_changed_);
}

void SimulationController::apply_pending_garment_placement(QOpenGLFunctions_4_5_Core& gl)
{
    if (!has_pending_garment_placement()) {
        return;
    }

    const bool update_rest_lengths = placement_scale_changed_;
    if (!scene_.update_garment_placement(editable_garment_id_, pending_position_offset_, pending_scale_)) {
        placement_position_changed_ = false;
        placement_scale_changed_ = false;
        return;
    }

    for (const GarmentObject& garment : scene_.garments()) {
        if (garment.id == editable_garment_id_) {
            gpu_state_.update_garment_placement(garment, update_rest_lengths, gl);
            break;
        }
    }

    placement_position_changed_ = false;
    placement_scale_changed_ = false;
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
