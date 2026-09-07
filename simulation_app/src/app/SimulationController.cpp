#include "app/SimulationController.h"

#include "asset/AssetIO.h"
#include "simulation/SimulationParams.h"
#include "simulation/collision/MeshBvhBuilder.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include <QObject>

namespace {
GarmentObject build_garment(GarmentLayer layer, GarmentMesh mesh)
{
    GarmentObject garment;
    garment.layer = layer;
    garment.mesh = std::move(mesh);
    garment.triangle_bvh = MeshBvhBuilder(garment.mesh).build_triangle_bvh();
    return garment;
}

glm::mat4 make_placement_matrix(const glm::vec3& center, const glm::vec3& position_offset, float scale)
{
    glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, position_offset);
    matrix = glm::translate(matrix, center);
    matrix = glm::scale(matrix, glm::vec3{scale});
    matrix = glm::translate(matrix, -center);
    return matrix;
}
}

SimulationController::SimulationController(SimulationParams params)
    : params_(params),
      simulation_pipeline_(params)
{
    QObject::connect(&frame_timer_, &QTimer::timeout, this, &SimulationController::tick_frame);
}

SimulationController::~SimulationController()
{
    release_gpu();
}

// Initialization
void SimulationController::initialize(const std::filesystem::path& shader_dir,
                                      const std::filesystem::path& default_character_path,
                                      QOpenGLFunctions_4_5_Core& gl)
{
    load_default_character(default_character_path);
    initialize_gpu(shader_dir, gl);
    Q_EMIT camera_reset_requested(scene_.character_root_position(0));
    frame_timer_.setSingleShot(true);
    frame_timer_.setTimerType(Qt::PreciseTimer);
    update_frame_timer();
}

void SimulationController::initialize_gpu(const std::filesystem::path& shader_dir,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    if (is_gpu_initialized()) {
        throw std::runtime_error("Simulation GPU state is already initialized.");
    }

    gpu_state_.initialize(shader_dir,
                          params_.constraints.attachment_surface_offset,
                          params_.collisions.body.detection_distance,
                          scene_,
                          gl);
    simulation_pipeline_.initialize(shader_dir, gl);
    scene_renderer_.initialize(shader_dir, gl);
}

void SimulationController::load_default_character(const std::filesystem::path& default_character_path)
{
    std::vector<std::uint8_t> triangle_part_labels;
    CharacterMotion motion = asset_io::read_default_character(default_character_path, triangle_part_labels);

    MeshBvhBuilder body_bvh_builder(motion, triangle_part_labels);
    scene_.set_body_bvhs(body_bvh_builder.build_triangle_bvh(),
                         body_bvh_builder.build_vertex_bvh(),
                         body_bvh_builder.build_edge_bvh());

    default_character_motion_ = std::move(motion);
    scene_.set_character_motion(default_character_motion_);
    is_default_pose_ = true;
}

void SimulationController::set_run_with_gl_context(std::function<void(GlContextTask)> run_with_gl_context)
{
    run_with_gl_context_ = std::move(run_with_gl_context);
}

// Simulation and rendering
void SimulationController::start_simulation()
{
    simulation_running_ = true;
}

void SimulationController::stop_simulation()
{
    simulation_running_ = false;
}

void SimulationController::draw(const glm::mat4& mvp, bool is_placement_active, QOpenGLFunctions_4_5_Core& gl)
{
    scene_renderer_.draw(scene_, gpu_state_, make_placement_matrices(), mvp, is_placement_active, gl);
}

void SimulationController::tick_frame()
{
    assert(is_gpu_initialized());
    update_frame_timer();

    if (simulation_running_) {
        run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
            if (scene_.garments().empty()) {
                const float frame_position = params_.step.motion_frame_position(motion_step_index_ + 1u, 0);
                const float frame_alpha = scene_.motion_frame_alpha(frame_position);
                gpu_state_.update_character_pose(scene_, frame_alpha, gl);
            } else {
                simulation_pipeline_.step(scene_, gpu_state_, motion_step_index_, gl);
            }
            ++motion_step_index_;
            scene_.set_motion_frame_index(motion_step_index_ / params_.step.motion_stride());
        });

        Q_EMIT camera_target_changed(scene_.character_root_position(scene_.motion_frame_index()));
    }

    Q_EMIT viewport_update_requested();
}

void SimulationController::update_frame_timer()
{
    const auto interval = std::chrono::nanoseconds{std::chrono::seconds{1}} / params_.step.fps;

    next_frame_deadline_ += interval;
    if (next_frame_deadline_.hasExpired()) {
        next_frame_deadline_ = QDeadlineTimer(interval, Qt::PreciseTimer);
    }
    frame_timer_.start(next_frame_deadline_.remainingTime());
}

// Character
bool SimulationController::set_character_motion(CharacterMotion motion)
{
    assert(is_gpu_initialized());

    if (motion.vertex_count != default_character_motion_.vertex_count ||
        motion.triangle_vertex_indices != default_character_motion_.triangle_vertex_indices) {
        return false;
    }

    run_with_gl_context_([this, &motion](QOpenGLFunctions_4_5_Core& gl) {
        if (is_default_pose_) {
            gpu_state_.capture_garment_base_positions(gl);
        } else {
            gpu_state_.restore_garment_base_positions(gl);
        }
        set_character_motion_state(std::move(motion), gl);
        is_default_pose_ = false;
    });

    return true;
}

void SimulationController::reset_scene()
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_running_ = false;
        reset_garment_placements();

        scene_.clear_garments();
        gpu_state_.release_garment_resources(gl);
        set_character_motion_state(default_character_motion_, gl);
        is_default_pose_ = true;
    });
}

void SimulationController::return_to_default_pose()
{
    assert(is_gpu_initialized());

    simulation_running_ = false;
    if (is_default_pose_) {
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        gpu_state_.restore_garment_base_positions(gl);

        reset_garment_placements();
        set_character_motion_state(default_character_motion_, gl);
        gpu_state_.clear_garment_base_positions(gl);
        is_default_pose_ = true;
    });
}

void SimulationController::set_character_motion_state(CharacterMotion motion, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_motion(std::move(motion));
    gpu_state_.set_character_motion(scene_, gl);
    motion_step_index_ = 0;
    Q_EMIT camera_reset_requested(scene_.character_root_position(0));
}

// Garment placement
void SimulationController::set_garment_mesh(GarmentLayer layer, GarmentMesh mesh)
{
    assert(is_gpu_initialized() && !is_simulation_running() && is_default_pose_);

    GarmentObject garment = build_garment(layer, std::move(mesh));

    run_with_gl_context_([this, layer, &garment](QOpenGLFunctions_4_5_Core& gl) {
        scene_.set_garment(std::move(garment));
        gpu_state_.rebuild_garment_resources(scene_, gl, layer);

        garment_placement_states_[layer].emplace();
    });
}

void SimulationController::set_garment_placement(GarmentLayer layer,
                                                 const glm::vec3& position_offset,
                                                 float scale)
{
    garment_placement_states_[layer]->position_offset = position_offset;
    garment_placement_states_[layer]->scale = scale;
}

void SimulationController::set_garment_color(GarmentLayer layer, const glm::vec3& color)
{
    scene_.update_garment_color(layer, color);
}

void SimulationController::confirm_garment_placement()
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        std::vector<const GarmentObject*> placement_garments;

        for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
            const auto& placement = garment_placement_states_[layer];
            if (!placement) {
                continue;
            }

            const auto& garment = scene_.place_garment(layer, placement->position_offset, placement->scale);
            placement_garments.push_back(&garment);
            gpu_state_.upload_garment_placement(garment, gl);
        }

        gpu_state_.rebuild_collision_buffers(gl);
        simulation_pipeline_.prefit_garments(gpu_state_, placement_garments, gl);

        for (const GarmentObject* garment : placement_garments) {
            gpu_state_.initialize_garment_attachments(*garment, gl);
        }

        reset_garment_placements();
    });
}

void SimulationController::discard_garment_placement(GarmentLayer layer)
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this, layer](QOpenGLFunctions_4_5_Core& gl) {
        if (scene_.remove_garment(layer)) {
            gpu_state_.rebuild_garment_resources(scene_, gl, layer);
        }
        garment_placement_states_[layer].reset();
    });
}

void SimulationController::cancel_placement_session()
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        const bool clear_all = garment_placement_states_[GarmentLayer::Lower].has_value();
        reset_garment_placements();

        if (clear_all) {
            scene_.clear_garments();
            gpu_state_.release_garment_resources(gl);
        } else {
            scene_.remove_garment(GarmentLayer::Upper);
            gpu_state_.rebuild_garment_resources(scene_, gl, GarmentLayer::Upper);
        }
    });
}

std::array<glm::mat4, 2> SimulationController::make_placement_matrices() const
{
    std::array<glm::mat4, 2> placement_matrices{glm::mat4{1.0f}, glm::mat4{1.0f}};

    for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
        const std::optional<GarmentPlacementState>& placement = garment_placement_states_[layer];
        if (!placement) {
            continue;
        }

        const GarmentObject* garment = scene_.find_garment(layer);
        if (!garment) {
            continue;
        }

        placement_matrices[layer] =
            make_placement_matrix(garment->mesh.bounds_center, placement->position_offset, placement->scale);
    }
    return placement_matrices;
}

void SimulationController::reset_garment_placements()
{
    for (std::optional<GarmentPlacementState>& placement : garment_placement_states_) {
        placement.reset();
    }
}

// State
bool SimulationController::is_gpu_initialized() const
{
    return gpu_state_.is_initialized() &&
           simulation_pipeline_.is_initialized() &&
           scene_renderer_.is_initialized();
}

bool SimulationController::can_start_garment_placement() const
{
    const bool has_active_placement = std::any_of(
        garment_placement_states_.begin(),
        garment_placement_states_.end(),
        [](const std::optional<GarmentPlacementState>& placement) { return placement.has_value(); });
    return has_active_placement || scene_.garments().size() < 2u;
}

// Accessors
bool SimulationController::is_simulation_running() const
{
    return simulation_running_;
}

std::size_t SimulationController::garment_count() const
{
    return scene_.garments().size();
}

// Release
void SimulationController::release_gpu()
{
    if (!is_gpu_initialized()) {
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) { release_gpu(gl); });
}

void SimulationController::release_gpu(QOpenGLFunctions_4_5_Core& gl)
{
    scene_renderer_.release(gl);
    simulation_pipeline_.release(gl);
    gpu_state_.release(gl);
}
