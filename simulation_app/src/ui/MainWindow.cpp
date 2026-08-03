#include "ui/MainWindow.h"

#include "asset/AssetIO.h"
#include "simulation/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/PlacementController.h"
#include "ui/Viewport.h"
#include "utils/QtUtils.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QWidget>

namespace {
constexpr int initial_window_width = 1440;
constexpr int initial_window_height = 900;
constexpr int minimum_window_width = 1000;
constexpr int minimum_window_height = 600;
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
    simulation_controller_ = std::make_unique<SimulationController>();
    placement_controller_ = std::make_unique<PlacementController>(*simulation_controller_,
                                                                  viewport_->placement_panel(),
                                                                  viewport_->garment_color_panel(),
                                                                  viewport_->garment_cards_panel(),
                                                                  *viewport_);
    placement_controller_->set_active_changed_callback([this]() {
        update_simulation_button_state();
        update_asset_button_state();
    });
    placement_controller_->set_layout_changed_callback([this]() { viewport_->update_layout(); });

    setCentralWidget(viewport_);

    setup_viewport_callbacks();
    setup_asset_browser_callbacks();
    setup_simulation_control_callbacks();

    // initial UI state
    viewport_->update_layout();
}

MainWindow::~MainWindow()
{
    AssetBrowserPanel& asset_browser_panel = viewport_->asset_browser_panel();
    asset_browser_panel.set_motion_loading_changed_callback({});
    asset_browser_panel.set_motion_loaded_callback({});
    asset_browser_panel.set_garment_load_started_callback({});
    asset_browser_panel.set_garment_loaded_callback({});
    viewport_->set_play_pause_callback({});
    viewport_->set_default_pose_callback({});
    viewport_->set_reset_callback({});
    placement_controller_.reset();
    simulation_controller_->release_gpu();
    simulation_controller_->set_viewport_callbacks({});
    viewport_->set_initialize_callback({});
    viewport_->set_scene_render_callback({});
}

// Initialization
bool MainWindow::initialize_scene(QOpenGLFunctions_4_5_Core& gl)
{
    CharacterMesh character_mesh;
    std::vector<std::uint8_t> triangle_part_labels;
    if (!asset_io::read_default_character(project_paths_.default_character_path,
                                          character_mesh,
                                          triangle_part_labels)) {
        return false;
    }

    return simulation_controller_->initialize(project_paths_.shaders,
                                              std::move(character_mesh),
                                              triangle_part_labels,
                                              gl);
}

void MainWindow::setup_viewport_callbacks()
{
    // Controller callbacks
    SimulationController::ViewportCallbacks callbacks;

    callbacks.is_ready = [this]() { return viewport_->is_gl_initialized(); };

    callbacks.run_with_gl_context = [this](SimulationController::GlContextTask task) {
        run_with_gl_context(*viewport_, [this, &task]() { task(viewport_->gl_functions()); });
    };

    callbacks.request_update = [this]() { viewport_->update(); };

    callbacks.reset_camera_to_character_root = [this](const glm::vec3& root_position) {
        viewport_->reset_camera_to_character_root(root_position);
    };

    callbacks.set_camera_target = [this](const glm::vec3& root_position) {
        viewport_->set_camera_target(root_position);
    };

    simulation_controller_->set_viewport_callbacks(std::move(callbacks));

    // Scene initialization
    viewport_->set_initialize_callback(
        [this](QOpenGLFunctions_4_5_Core& gl) { return initialize_scene(gl); });

    // Scene rendering
    viewport_->set_scene_render_callback([this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
        if (!simulation_controller_->is_gpu_initialized()) {
            return;
        }

        const float character_opacity =
            placement_controller_->is_active() ? placement_character_opacity : 1.0f;
        simulation_controller_->draw(mvp, character_opacity, gl);
    });
}

void MainWindow::setup_asset_browser_callbacks()
{
    AssetBrowserPanel& asset_browser_panel = viewport_->asset_browser_panel();
    asset_browser_panel.set_motion_loading_changed_callback([this](bool is_loading) {
        if (is_loading) {
            simulation_controller_->stop_simulation();
        }
        viewport_->set_motion_loading(is_loading);
        if (!is_loading) {
            update_simulation_button_state();
        }
    });
    asset_browser_panel.set_motion_loaded_callback([this](CharacterMesh mesh) {
        placement_controller_->end_session();
        simulation_controller_->set_character_mesh(std::move(mesh));
        simulation_controller_->start_simulation();
    });
    asset_browser_panel.set_garment_load_started_callback([this]() {
        simulation_controller_->return_to_default_pose();
        update_simulation_button_state();
    });
    asset_browser_panel.set_garment_loaded_callback(
        [this](const std::filesystem::path& asset_path, GarmentMesh mesh) {
            placement_controller_->load_garment(asset_path, std::move(mesh));
        });
}

void MainWindow::setup_simulation_control_callbacks()
{
    viewport_->set_play_pause_callback([this]() {
        if (simulation_controller_->is_simulation_running()) {
            simulation_controller_->stop_simulation();
        } else {
            simulation_controller_->start_simulation();
        }
        update_simulation_button_state();
    });

    viewport_->set_default_pose_callback([this]() {
        simulation_controller_->return_to_default_pose();
        update_simulation_button_state();
    });

    viewport_->set_reset_callback([this]() {
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
