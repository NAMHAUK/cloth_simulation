#include "ui/MainWindow.h"

#include "app/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/PlacementController.h"
#include "ui/Viewport.h"
#include "utils/QtUtils.h"

#include <exception>
#include <memory>
#include <utility>

#include <QMessageBox>
#include <QWidget>
#include <QtLogging>

namespace {
constexpr int initial_window_width = 1440;
constexpr int initial_window_height = 900;
constexpr int minimum_window_width = 1000;
constexpr int minimum_window_height = 700;
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
    connect_viewport_rendering();

    setCentralWidget(viewport_);

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
    try {
        simulation_controller_->initialize(project_paths_.shader_dir,
                                           project_paths_.default_character_path,
                                           gl);
    } catch (const std::exception& error) {
        qFatal("Application initialization failed: %s", error.what());
    }
}

void MainWindow::connect_viewport_rendering()
{
    connect(viewport_,
            &Viewport::scene_initialization_requested,
            this,
            &MainWindow::initialize_scene,
            Qt::DirectConnection);

    connect(
        viewport_,
        &Viewport::scene_render_requested,
        this,
        [this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
            simulation_controller_->draw(mvp, placement_controller_->is_active(), gl);
        },
        Qt::DirectConnection);
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
    asset_browser_panel.set_motion_loaded_callback([this](CharacterMotion motion) {
        if (!simulation_controller_->set_character_motion(std::move(motion))) {
            QMessageBox::warning(this, "Load Failed", "Motion topology does not match the character.");
            return false;
        }
        simulation_controller_->start_simulation();
        return true;
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
        simulation_controller_->reset_scene();
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
