#include "simulation/SimulationController.h"

#include "gpu/bvh/MeshBvhBuilder.h"
#include "simulation/SimulationParams.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <QObject>

namespace {
GarmentObject build_garment(GarmentLayer layer, GarmentMesh mesh)
{
    GarmentObject garment;
    garment.layer = layer;
    garment.source_mesh = mesh;
    garment.mesh = std::move(mesh);
    garment.garment_triangle_bvh = MeshBvhBuilder(garment.mesh).build_triangle_bvh();
    return garment;
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
void SimulationController::set_run_with_gl_context(std::function<void(GlContextTask)> run_with_gl_context)
{
    run_with_gl_context_ = std::move(run_with_gl_context);
}

void SimulationController::initialize(const std::filesystem::path& shader_dir,
                                      CharacterMesh character_mesh,
                                      const std::vector<std::uint8_t>& triangle_part_labels,
                                      QOpenGLFunctions_4_5_Core& gl)
{
    initialize_gpu(shader_dir, gl);
    load_default_character(std::move(character_mesh), triangle_part_labels, gl);
    frame_timer_.start(params_.step.tick_ms());
}

void SimulationController::initialize_gpu(const std::filesystem::path& shader_dir,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    if (is_gpu_initialized()) {
        throw std::runtime_error("Simulation GPU state is already initialized.");
    }

    gpu_state_.initialize(shader_dir, gl);
    simulation_pipeline_.initialize(shader_dir, gl);
    render_pipeline_.initialize(shader_dir, gl);
}

void SimulationController::load_default_character(CharacterMesh mesh,
                                                  const std::vector<std::uint8_t>& triangle_part_labels,
                                                  QOpenGLFunctions_4_5_Core& gl)
{
    MeshBvhBuilder bvh_builder(mesh, triangle_part_labels);
    scene_.set_body_bvhs(bvh_builder.build_triangle_bvh(),
                         bvh_builder.build_vertex_bvh(),
                         bvh_builder.build_edge_bvh());

    default_character_mesh_ = std::move(mesh);
    set_character_mesh_state(default_character_mesh_, gl);
    is_default_pose_ = true;
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

void SimulationController::draw(const glm::mat4& mvp, float character_opacity, QOpenGLFunctions_4_5_Core& gl)
{
    render_pipeline_.draw(scene_, gpu_state_, mvp, character_opacity, gl);
}

void SimulationController::tick_frame()
{
    assert(is_gpu_initialized());

    if (simulation_running_) {
        run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
            if (scene_.garments().empty()) {
                simulation_pipeline_.step_character_only(scene_, gpu_state_, motion_step_index_, gl);
            } else {
                simulation_pipeline_.step(scene_, gpu_state_, motion_step_index_, gl);
            }
            ++motion_step_index_;
            scene_.update_character_frame(motion_step_index_ / params_.step.motion_stride());
        });
    }

    if (simulation_running_) {
        Q_EMIT camera_target_changed(scene_.character_root_position(scene_.current_character_frame()));
    }

    Q_EMIT viewport_update_requested();
}

// Character
void SimulationController::set_character_mesh(CharacterMesh mesh)
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        if (!scene_.garments().empty() && !gpu_state_.restore_base_positions(gl)) {
            gpu_state_.save_base_positions(gl);
        }

        set_character_mesh_state(std::move(mesh), gl);
        is_default_pose_ = false;
    });

    Q_EMIT viewport_update_requested();
}

void SimulationController::reset_scene_to_default()
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_running_ = false;
        reset_garment_placements();

        scene_.clear_garments();
        gpu_state_.update_garment_meshes(scene_, gl);
        gpu_state_.clear_base_positions(gl);
        set_character_mesh_state(default_character_mesh_, gl);
        is_default_pose_ = true;
    });

    Q_EMIT viewport_update_requested();
}

void SimulationController::return_to_default_pose()
{
    simulation_running_ = false;
    if (is_default_pose_) {
        return;
    }

    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        if (!scene_.garments().empty() && !gpu_state_.restore_base_positions(gl)) {
            return;
        }

        reset_garment_placements();
        set_character_mesh_state(default_character_mesh_, gl);
        is_default_pose_ = true;
    });

    Q_EMIT viewport_update_requested();
}

void SimulationController::set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_mesh(std::move(mesh));
    gpu_state_.set_character_mesh(scene_, params_.collisions.body.thickness, gl);
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
        gpu_state_.update_garment_meshes(scene_, gl, layer);

        garment_placements_[layer].reset();
        garment_placements_[layer].is_active = true;
        gpu_state_.clear_base_positions(gl);
    });

    Q_EMIT viewport_update_requested();
}

bool SimulationController::remove_garment_placement(GarmentLayer layer)
{
    assert(is_gpu_initialized());

    GarmentPlacementState& placement = garment_placements_[layer];

    bool garment_removed = false;
    run_with_gl_context_([this, layer, &placement, &garment_removed](QOpenGLFunctions_4_5_Core& gl) {
        garment_removed = scene_.remove_garment(layer);
        if (garment_removed) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }
        placement.reset();
        gpu_state_.clear_base_positions(gl);
    });

    Q_EMIT viewport_update_requested();
    return garment_removed;
}

void SimulationController::set_garment_placement(GarmentLayer layer,
                                                 const glm::vec3& position_offset,
                                                 float scale)
{
    GarmentPlacementState& placement = garment_placements_[layer];
    if (placement.position_offset != position_offset) {
        placement.position_offset = position_offset;
        placement.position_changed = true;
    }
    if (placement.scale != scale) {
        placement.scale = scale;
        placement.scale_changed = true;
    }

    Q_EMIT viewport_update_requested();
}

void SimulationController::set_garment_color(GarmentLayer layer, const glm::vec3& color)
{
    scene_.update_garment_color(layer, color);
    Q_EMIT viewport_update_requested();
}

bool SimulationController::confirm_garment_placement()
{
    assert(is_gpu_initialized());

    bool placement_confirmed = false;
    run_with_gl_context_([this, &placement_confirmed](QOpenGLFunctions_4_5_Core& gl) {
        apply_garment_placement_changes(gl);
        std::vector<GarmentLayer> unconfirmed_layers;
        for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
            if (garment_placements_[layer].is_active) {
                unconfirmed_layers.push_back(layer);
            }
        }
        simulation_pipeline_.prefit_garments(gpu_state_, unconfirmed_layers, gl);

        for (GarmentLayer layer : unconfirmed_layers) {
            if (!gpu_state_.build_garment_attachment_targets(scene_,
                                                             layer,
                                                             params_.constraints.attachment_surface_offset,
                                                             gl)) {
                std::cerr << "Cannot confirm garment placement because attachment target creation failed.\n";
                restore_garment_placements(unconfirmed_layers, gl);
                return;
            }
        }

        reset_garment_placements();
        gpu_state_.clear_base_positions(gl);
        placement_confirmed = true;
    });

    Q_EMIT viewport_update_requested();
    return placement_confirmed;
}

void SimulationController::cancel_garment_placement()
{
    assert(is_gpu_initialized());

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        bool garment_removed = false;
        for (std::size_t index = 0; index < garment_placements_.size(); ++index) {
            GarmentPlacementState& placement = garment_placements_[index];
            if (placement.is_active && scene_.remove_garment(static_cast<GarmentLayer>(index))) {
                garment_removed = true;
            }
            placement.reset();
        }
        if (garment_removed) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }

        gpu_state_.clear_base_positions(gl);
    });

    Q_EMIT viewport_update_requested();
}

void SimulationController::apply_garment_placement_changes(QOpenGLFunctions_4_5_Core& gl)
{
    for (std::size_t index = 0; index < garment_placements_.size(); ++index) {
        GarmentPlacementState& placement = garment_placements_[index];
        if (!placement.has_update()) {
            continue;
        }

        const auto layer = static_cast<GarmentLayer>(index);
        const bool update_rest_lengths = placement.scale_changed;
        const GarmentObject* garment =
            scene_.update_garment_placement(layer, placement.position_offset, placement.scale);
        if (garment != nullptr) {
            gpu_state_.update_garment_placement(*garment, update_rest_lengths, gl);
        }
        placement.clear_updates();
    }
}

void SimulationController::restore_garment_placements(const std::vector<GarmentLayer>& layers,
                                                      QOpenGLFunctions_4_5_Core& gl)
{
    for (GarmentLayer layer : layers) {
        gpu_state_.deactivate_garment_attachment_targets(layer);
        const GarmentObject* garment = scene_.find_garment(layer);
        assert(garment != nullptr);
        gpu_state_.update_garment_placement(*garment, false, gl);
    }
}

void SimulationController::reset_garment_placements()
{
    for (GarmentPlacementState& placement : garment_placements_) {
        placement.reset();
    }
}

// State
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

std::size_t SimulationController::garment_count() const
{
    return scene_.garments().size();
}

bool SimulationController::can_start_garment_placement() const
{
    const bool has_active_placement =
        std::any_of(garment_placements_.begin(),
                    garment_placements_.end(),
                    [](const GarmentPlacementState& placement) { return placement.is_active; });
    return has_active_placement || !scene_.has_multiple_garments();
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
    render_pipeline_.release(gl);
    simulation_pipeline_.release(gl);
    gpu_state_.release(gl);
}
