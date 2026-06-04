#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <QMainWindow>

#include "app/ProjectPaths.h"
#include "asset/MotionConverter.h"

class AssetBrowserPanel;
class AssetLoader;
class MotionConverter;
class QObject;
class SimulationController;
class SceneViewport;
class QEvent;
class QWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setup_callbacks();
    void update_viewer_layout();
    void refresh_motion_list();
    void refresh_garment_list();
    void request_garment_conversion();
    void request_amass_conversion();
    std::optional<ConverterCommand> prepare_amass_conversion();
    std::optional<ConverterCommand> prepare_garment_conversion();

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    MotionConverter* motion_converter_ = nullptr;
    MotionConverter* garment_converter_ = nullptr;
    QWidget* viewer_container_ = nullptr;
    AssetBrowserPanel* browser_panel_ = nullptr;
    SceneViewport* simulation_viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
};
