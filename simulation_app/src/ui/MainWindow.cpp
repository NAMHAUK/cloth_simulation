#include "ui/MainWindow.h"

#include "asset/AssetIO.h"
#include "simulation/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/PlacementController.h"
#include "ui/Viewport.h"
#include "utils/QtUtils.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QWidget>

namespace {
constexpr int initial_window_width = 1440;
constexpr int initial_window_height = 900;
constexpr int minimum_window_width = 1000;
constexpr int minimum_window_height = 700;
constexpr float placement_character_opacity = 0.3f;
}

MainWindow::MainWindow(const std::filesystem::path& project_root, QWidget* parent)
    : QMainWindow(parent),
      project_paths_(make_project_paths(project_root))
{
    setWindowTitle("SIMULATION APP");
    setMinimumSize(minimum_window_width, minimum_window_height);
    resize(initial_window_width, initial_window_height);

    viewport_ = new Viewport(project_paths_, this);

    setup_simulation_controller();
    setup_placement_controller();

    setCentralWidget(viewport_);

    setup_viewport_render_callbacks();
    setup_asset_browser_callbacks();
    connect_simulation_controls();

    // initial UI state
    viewport_->update_layout();
}

MainWindow::~MainWindow()
{
    AssetBrowserPanel& asset_browser_panel = viewport_->asset_browser_panel();
    asset_browser_panel.set_motion_loaded_callback({});
    asset_browser_panel.set_garment_loaded_callback({});
    placement_controller_.reset();
    viewport_->set_initialize_callback({});
    viewport_->set_scene_render_callback({});
}

// Initialization
void MainWindow::setup_simulation_controller()
{
    simulation_controller_ = std::make_unique<SimulationController>();

    Viewport* const viewport = viewport_;

    auto run_gl_task = [viewport](SimulationController::GlContextTask task) {
        run_with_gl_context(*viewport, [viewport, &task]() { task(viewport->gl_functions()); });
    };
    simulation_controller_->set_run_with_gl_context(std::move(run_gl_task));

    connect(simulation_controller_.get(),
            &SimulationController::viewport_update_requested,
            viewport,
            qOverload<>(&Viewport::update));
    connect(simulation_controller_.get(),
            &SimulationController::camera_reset_requested,
            viewport,
            &Viewport::reset_camera);
    connect(simulation_controller_.get(),
            &SimulationController::camera_target_changed,
            viewport,
            &Viewport::set_camera_target);
}

void MainWindow::setup_placement_controller()
{
    placement_controller_ = std::make_unique<PlacementController>(*simulation_controller_,
                                                                  viewport_->placement_panel(),
                                                                  viewport_->garment_color_panel(),
                                                                  viewport_->garment_cards_panel());
    connect(placement_controller_.get(), &PlacementController::active_changed, this, [this]() {
        update_simulation_button_state();
        update_asset_button_state();
    });
    connect(placement_controller_.get(),
            &PlacementController::layout_changed,
            viewport_,
            &Viewport::update_layout);
}

void MainWindow::initialize_scene(QOpenGLFunctions_4_5_Core& gl)
{
    CharacterMesh character_mesh;
    std::vector<std::uint8_t> triangle_part_labels;
    if (!asset_io::read_default_character(project_paths_.default_character_path,
                                          character_mesh,
                                          triangle_part_labels)) {
        throw std::runtime_error("Failed to load the default character asset.");
    }

    simulation_controller_->initialize(project_paths_.shader_dir,
                                       std::move(character_mesh),
                                       triangle_part_labels,
                                       gl);
}

void MainWindow::setup_viewport_render_callbacks()
{
    // Scene initialization
    viewport_->set_initialize_callback([this](QOpenGLFunctions_4_5_Core& gl) { initialize_scene(gl); });

    // Scene rendering
    viewport_->set_scene_render_callback([this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
        if (!simulation_controller_->is_gpu_initialized()) {
            return;
        }

        simulation_controller_->apply_garment_placement_changes(gl);
        const float character_opacity =
            placement_controller_->is_active() ? placement_character_opacity : 1.0f;
        simulation_controller_->draw(mvp, character_opacity, gl);
    });
}

void MainWindow::setup_asset_browser_callbacks()
{
    AssetBrowserPanel& asset_browser_panel = viewport_->asset_browser_panel();
    connect(&asset_browser_panel, &AssetBrowserPanel::motion_loading_changed, this, [this](bool is_loading) {
        if (is_loading) {
            simulation_controller_->stop_simulation();
        }
        viewport_->set_loading_overlay_active(is_loading);
        if (!is_loading) {
            update_simulation_button_state();
        }
    });
    asset_browser_panel.set_motion_loaded_callback([this](CharacterMesh mesh) {
        simulation_controller_->set_character_mesh(std::move(mesh));
        simulation_controller_->start_simulation();
    });
    asset_browser_panel.set_garment_loaded_callback(
        [this](const std::filesystem::path& asset_path, GarmentMesh mesh) {
            simulation_controller_->return_to_default_pose();
            placement_controller_->load_garment(asset_path, std::move(mesh));
            update_simulation_button_state();
        });
}

void MainWindow::connect_simulation_controls()
{
    connect(viewport_, &Viewport::play_pause_requested, this, [this]() {
        if (simulation_controller_->is_simulation_running()) {
            simulation_controller_->stop_simulation();
        } else {
            simulation_controller_->start_simulation();
        }
        update_simulation_button_state();
    });

    connect(viewport_, &Viewport::default_pose_requested, this, [this]() {
        simulation_controller_->return_to_default_pose();
        update_simulation_button_state();
    });

    connect(viewport_, &Viewport::reset_requested, this, [this]() {
        placement_controller_->reset();
        simulation_controller_->reset_scene_to_default();
        update_simulation_button_state();
        update_asset_button_state();
    });
}

// UI updates
void MainWindow::update_simulation_button_state()
{
    viewport_->set_simulation_button_state(simulation_controller_->is_simulation_running(),
                                           !placement_controller_->is_active());
}

void MainWindow::update_asset_button_state()
{
    AssetBrowserPanel& asset_browser_panel = viewport_->asset_browser_panel();
    asset_browser_panel.set_motion_selection_enabled(!placement_controller_->is_active());
    asset_browser_panel.set_garment_selection_enabled(simulation_controller_->can_start_garment_placement());
}
