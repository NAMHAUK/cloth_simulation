#pragma once

#include <filesystem>
#include <memory>

#include <QMainWindow>

#include "app/ProjectPaths.h"

class PlacementController;
class SimulationController;
class Viewport;
class QOpenGLFunctions_4_5_Core;
class QWidget;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    // Initialization
    void setup_simulation_controller();
    void setup_placement_controller();
    bool initialize_scene(QOpenGLFunctions_4_5_Core& gl);
    void setup_viewport_render_callbacks();
    void setup_asset_browser_callbacks();
    void setup_simulation_control_callbacks();

    // UI updates
    void update_simulation_button_state();
    void update_asset_button_state();

    ProjectPaths project_paths_;
    Viewport* viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
    std::unique_ptr<PlacementController> placement_controller_;
};
