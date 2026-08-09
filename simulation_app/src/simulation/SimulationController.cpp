#include "simulation/SimulationController.h"

#include "app/ProjectPaths.h"
#include "gpu/bvh/MeshBvhBuilder.h"
#include "simulation/SimulationSettings.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

#include <QObject>

SimulationController::SimulationController()
{
    // tick마다 frame update 함수 설정
    QObject::connect(&frame_timer_, &QTimer::timeout, &frame_timer_, [this]() { tick_frame(); });
}

SimulationController::~SimulationController()
{
    release_gpu();
}

// Initialization //
void SimulationController::set_run_with_gl_context(std::function<void(GlContextTask)> run_with_gl_context)
{
    run_with_gl_context_ = std::move(run_with_gl_context);
}

bool SimulationController::initialize(const ShaderPaths& shader_paths,
                                      CharacterMesh character_mesh,
                                      const std::vector<std::uint8_t>& triangle_part_labels,
                                      QOpenGLFunctions_4_5_Core& gl)
{
    return initialize_gpu(shader_paths, gl) &&
           load_default_character(std::move(character_mesh), triangle_part_labels, gl);
}

bool SimulationController::initialize_gpu(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl)
{
    assert(!is_gpu_initialized());
    if (is_gpu_initialized()) {
        return false;
    }

    if (!gpu_state_.initialize(shader_paths, gl) ||
        !simulation_pipeline_.initialize(shader_paths, gl) ||
        !render_pipeline_.initialize(shader_paths, gl)) {

        render_pipeline_.release(gl);
        simulation_pipeline_.release(gl);
        gpu_state_.release(gl);
        return false;
    }

    frame_timer_.start(simulation_settings::simulation_tick_ms);
    return true;
}

// Rendering //

void SimulationController::draw(const glm::mat4& mvp, float character_opacity, QOpenGLFunctions_4_5_Core& gl)
{
    render_pipeline_.draw(scene_, gpu_state_, mvp, character_opacity, gl);
}

void SimulationController::tick_frame()
{
    if (!is_gpu_initialized()) {
        return;
    }

    if (simulation_running_ || has_garment_placement_update()) {
        run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
            set_current_garment_placement(gl);

            if (simulation_running_) {
                simulation_pipeline_.step(scene_, gpu_state_, motion_step_index_, gl);
                ++motion_step_index_;
                scene_.update_character_frame(motion_step_index_,
                                              simulation_settings::character_frame_stride);
            }
        });
    }

    if (simulation_running_) {
        Q_EMIT camera_target_changed(scene_.character_root_position(scene_.current_character_frame()));
    }

    Q_EMIT viewport_update_requested();
}

// Object //

bool SimulationController::load_default_character(CharacterMesh mesh,
                                                  const std::vector<std::uint8_t>& triangle_part_labels,
                                                  QOpenGLFunctions_4_5_Core& gl)
{
    MeshBvhBuilder bvh_builder(mesh.vertex_count,
                               mesh.triangle_vertex_indices,
                               mesh.vertices,
                               triangle_part_labels,
                               body_bvh_excluded_part_mask);
    TriangleBvhData default_body_triangle_bvh_data = bvh_builder.build_triangle_bvh();
    if (!default_body_triangle_bvh_data.is_valid(mesh.triangle_count)) {
        std::cerr << "Failed to build default body triangle BVH.\n";
        return false;
    }

    VertexBvhData default_body_vertex_bvh_data = bvh_builder.build_vertex_bvh();
    if (!default_body_vertex_bvh_data.is_valid(mesh.vertex_count)) {
        std::cerr << "Failed to build default body vertex BVH.\n";
        return false;
    }

    EdgeBvhData default_body_edge_bvh_data = bvh_builder.build_edge_bvh();
    if (!default_body_edge_bvh_data.is_valid()) {
        std::cerr << "Failed to build default body edge BVH.\n";
        return false;
    }

    scene_.set_default_body_triangle_bvh_data(std::move(default_body_triangle_bvh_data));
    scene_.set_default_body_vertex_bvh_data(std::move(default_body_vertex_bvh_data));
    scene_.set_default_body_edge_bvh_data(std::move(default_body_edge_bvh_data));
    default_character_mesh_ = std::move(mesh);
    set_character_mesh_state(default_character_mesh_, gl);
    is_default_pose_ = true;
    return true;
}

void SimulationController::set_character_mesh(CharacterMesh mesh)
{
    if (!is_gpu_initialized()) {
        std::cerr << "Cannot set character mesh before GPU initialization.\n";
        return;
    }

    run_with_gl_context_([this, &mesh](QOpenGLFunctions_4_5_Core& gl) {
        if (!scene_.garments().empty()) {
            if (!has_base_positions_) {
                has_base_positions_ = gpu_state_.save_base_positions(gl);
            }

            if (has_base_positions_) {
                gpu_state_.restore_base_positions(gl);
            }
        }

        set_character_mesh_state(std::move(mesh), gl);
    });

    Q_EMIT viewport_update_requested();
}

void SimulationController::set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl)
{
    scene_.set_character_mesh(std::move(mesh));
    gpu_state_.set_character_mesh(scene_, gl);
    motion_step_index_ = 0;
    is_default_pose_ = false;
    Q_EMIT camera_reset_requested(scene_.character_root_position(0));
}

bool SimulationController::set_garment_mesh(GarmentLayer layer, GarmentMesh mesh)
{
    if (!is_gpu_initialized()) {
        std::cerr << "Cannot set garment mesh before GPU initialization.\n";
        return false;
    }
    if (simulation_running_ || !is_default_pose_) {
        std::cerr << "Cannot set garment mesh before returning to the default pose.\n";
        return false;
    }

    GarmentPlacementState& placement = garment_placements_[layer];
    if (!placement.is_active && scene_.has_multiple_garments()) {
        std::cerr << "Cannot add more than two garment meshes.\n";
        return false;
    }

    bool garment_set = false;
    run_with_gl_context_([this, layer, &placement, &mesh, &garment_set](QOpenGLFunctions_4_5_Core& gl) {
        if (placement.is_active) {
            GarmentObject* existing_garment = scene_.find_garment(layer);
            if (existing_garment == nullptr) {
                return;
            }

            GarmentObject previous_garment = *existing_garment;
            if (!scene_.replace_garment_mesh(layer, std::move(mesh)) || !build_garment_triangle_bvh(layer)) {
                *existing_garment = std::move(previous_garment);
                std::cerr << "Failed to replace garment mesh.\n";
                return;
            }

            if (!gpu_state_.update_garment_meshes(scene_, gl, layer)) {
                *existing_garment = std::move(previous_garment);
                if (!gpu_state_.update_garment_meshes(scene_, gl, layer)) {
                    std::cerr << "Failed to restore garment GPU resources after replacement failure.\n";
                }
                return;
            }
            placement.position_offset = glm::vec3{0.0f};
            placement.scale = 1.0f;
            placement.clear_update();
        } else {
            // 새 garment 추가
            placement.clear();
            if (!scene_.add_garment_mesh(layer, std::move(mesh))) {
                std::cerr << "Failed to add garment mesh.\n";
                return;
            }
            placement.is_active = true;
            if (!build_garment_triangle_bvh(layer)) {
                scene_.remove_garment(layer);
                std::cerr << "Failed to build garment triangle BVH.\n";
                placement.clear();
                return;
            }
            if (!gpu_state_.update_garment_meshes(scene_, gl)) {
                scene_.remove_garment(layer);
                placement.clear();
                if (!gpu_state_.update_garment_meshes(scene_, gl)) {
                    std::cerr << "Failed to restore garment GPU resources after addition failure.\n";
                }
                return;
            }
        }

        gpu_state_.clear_base_positions(gl);
        has_base_positions_ = false;
        garment_set = true;
    });

    Q_EMIT viewport_update_requested();
    return garment_set;
}

void SimulationController::set_current_garment_placement(QOpenGLFunctions_4_5_Core& gl)
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
        placement.clear_update();
    }
}

bool SimulationController::build_garment_triangle_bvh(GarmentLayer layer)
{
    GarmentObject* garment = scene_.find_garment(layer);
    if (garment == nullptr) {
        return false;
    }

    const GarmentMesh& mesh = garment->mesh;
    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / 3u);
    const auto triangle_count = static_cast<std::uint32_t>(mesh.triangle_vertex_indices.size() / 3u);
    MeshBvhBuilder bvh_builder(vertex_count, mesh.triangle_vertex_indices, mesh.vertices);
    TriangleBvhData garment_triangle_bvh = bvh_builder.build_triangle_bvh();
    if (!garment_triangle_bvh.is_valid(triangle_count)) {
        return false;
    }

    garment->garment_triangle_bvh = std::move(garment_triangle_bvh);
    return true;
}

std::vector<GarmentLayer> SimulationController::garment_placement_layers() const
{
    std::vector<GarmentLayer> layers;
    layers.reserve(garment_placements_.size());
    for (std::size_t index = 0; index < garment_placements_.size(); ++index) {
        if (garment_placements_[index].is_active) {
            layers.push_back(static_cast<GarmentLayer>(index));
        }
    }
    return layers;
}

void SimulationController::restore_garment_placements(const std::vector<GarmentLayer>& layers,
                                                      QOpenGLFunctions_4_5_Core& gl)
{
    for (GarmentLayer layer : layers) {
        gpu_state_.deactivate_garment_attachment_targets(layer);
        const GarmentObject* garment = scene_.find_garment(layer);
        if (garment != nullptr) {
            gpu_state_.update_garment_placement(*garment, false, gl);
        }
    }
}

void SimulationController::clear_garment_placements()
{
    for (GarmentPlacementState& placement : garment_placements_) {
        placement.clear();
    }
}

void SimulationController::reset_scene_to_default()
{
    if (!is_gpu_initialized()) {
        std::cerr << "Cannot reset scene before GPU initialization.\n";
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        simulation_running_ = false;
        motion_step_index_ = 0;
        clear_garment_placements();

        scene_.clear_garments();
        gpu_state_.update_garment_meshes(scene_, gl);
        gpu_state_.clear_base_positions(gl);
        has_base_positions_ = false;
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

    if (!is_gpu_initialized()) {
        std::cerr << "Cannot return to default pose before GPU initialization.\n";
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        if (!scene_.garments().empty()) {
            if (!has_base_positions_ || !gpu_state_.restore_base_positions(gl)) {
                return;
            }
        }

        motion_step_index_ = 0;
        clear_garment_placements();
        set_character_mesh_state(default_character_mesh_, gl);
        is_default_pose_ = true;
    });

    Q_EMIT viewport_update_requested();
}

// garment placement panel //
bool SimulationController::remove_garment_placement(GarmentLayer layer)
{
    if (!is_gpu_initialized()) {
        return false;
    }

    GarmentPlacementState& placement = garment_placements_[layer];
    if (!placement.is_active) {
        return true;
    }

    bool garment_removed = false;
    run_with_gl_context_([this, layer, &placement, &garment_removed](QOpenGLFunctions_4_5_Core& gl) {
        garment_removed = scene_.remove_garment(layer);
        if (garment_removed) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }
        placement.clear();
        gpu_state_.clear_base_positions(gl);
        has_base_positions_ = false;
    });

    Q_EMIT viewport_update_requested();
    return garment_removed;
}

void SimulationController::set_garment_placement(GarmentLayer layer,
                                                 const glm::vec3& position_offset,
                                                 float scale)
{
    if (scale <= 0.0f) {
        return;
    }

    GarmentPlacementState& placement = garment_placements_[layer];
    if (!placement.is_active) {
        return;
    }

    if (placement.position_offset != position_offset) {
        placement.position_offset = position_offset;
        placement.position_changed = true;
    }
    if (placement.scale != scale) {
        placement.scale = scale;
        placement.scale_changed = true;
    }
}

void SimulationController::set_garment_color(GarmentLayer layer, const glm::vec3& color)
{
    if (!scene_.update_garment_color(layer, color)) {
        return;
    }

    if (!simulation_running_) {
        Q_EMIT viewport_update_requested();
    }
}

bool SimulationController::confirm_garment_placement()
{
    if (!is_gpu_initialized()) {
        std::cerr << "Cannot confirm garment placement before GPU initialization.\n";
        return false;
    }

    bool placement_confirmed = false;
    run_with_gl_context_([this, &placement_confirmed](QOpenGLFunctions_4_5_Core& gl) {
        set_current_garment_placement(gl);
        const std::vector<GarmentLayer> layers = garment_placement_layers();
        if (!simulation_pipeline_.prefit_garments(scene_, gpu_state_, layers, gl)) {
            std::cerr << "Cannot confirm garment placement because garment pre-fit failed.\n";
            restore_garment_placements(layers, gl);
            return;
        }

        for (GarmentLayer layer : layers) {
            if (!gpu_state_.build_garment_attachment_targets(scene_,
                                                             layer,
                                                             simulation_settings::attachment_surface_offset,
                                                             gl)) {
                std::cerr << "Cannot confirm garment placement because attachment target creation failed.\n";
                restore_garment_placements(layers, gl);
                return;
            }
        }

        clear_garment_placements();
        gpu_state_.clear_base_positions(gl);
        has_base_positions_ = false;
        placement_confirmed = true;
    });

    Q_EMIT viewport_update_requested();
    return placement_confirmed;
}

void SimulationController::cancel_garment_placement()
{
    if (!is_gpu_initialized()) {
        std::cerr << "Cannot cancel garment placement before GPU initialization.\n";
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        bool garment_removed = false;
        for (std::size_t index = 0; index < garment_placements_.size(); ++index) {
            GarmentPlacementState& placement = garment_placements_[index];
            if (placement.is_active && scene_.remove_garment(static_cast<GarmentLayer>(index))) {
                garment_removed = true;
            }
            placement.clear();
        }
        if (garment_removed) {
            gpu_state_.update_garment_meshes(scene_, gl);
        }

        gpu_state_.clear_base_positions(gl);
        has_base_positions_ = false;
    });

    Q_EMIT viewport_update_requested();
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

bool SimulationController::has_garment_placement_update() const
{
    return std::any_of(garment_placements_.begin(),
                       garment_placements_.end(),
                       [](const GarmentPlacementState& placement) { return placement.has_update(); });
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

void SimulationController::release_gpu()
{
    if (!is_gpu_initialized()) {
        return;
    }

    run_with_gl_context_([this](QOpenGLFunctions_4_5_Core& gl) {
        render_pipeline_.release(gl);
        simulation_pipeline_.release(gl);
        gpu_state_.release(gl);
    });
}
