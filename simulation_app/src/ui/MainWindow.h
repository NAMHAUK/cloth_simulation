#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <QMainWindow>

#include "app/ProjectPaths.h"
#include "asset/AssetConverter.h"

class AssetBrowserPanel;
class AssetLoader;
class QObject;
class SimulationController;
class SceneViewport;
class QEvent;
class QWidget;
enum class AssetPanelMode;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setup_callbacks();
    void setup_viewport_callbacks();
    void setup_browser_callbacks();
    void setup_asset_loader_callbacks();
    void setup_asset_converter_callbacks();
    void update_viewer_layout();
    void refresh_motion_list();
    void refresh_garment_list();
    void request_conversion(AssetPanelMode mode);
    std::optional<ConverterCommand> prepare_amass_conversion();
    std::optional<ConverterCommand> prepare_garment_conversion();

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    AssetConverter* motion_converter_ = nullptr;
    AssetConverter* garment_converter_ = nullptr;
    QWidget* viewer_container_ = nullptr;
    AssetBrowserPanel* browser_panel_ = nullptr;
    SceneViewport* simulation_viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
};
