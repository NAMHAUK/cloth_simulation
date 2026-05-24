#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <QMainWindow>

#include "app/ProjectPaths.h"
#include "io/MotionAsset.h"

class AssetLoader;
class MotionConverter;
class MotionBrowserPanel;
class QObject;
class AppController;
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
    void request_garment_asset_selection();
    void request_amass_conversion();
    std::optional<ConverterCommand> prepare_amass_conversion();

    ProjectPaths project_paths_;
    std::vector<MotionAsset> motions_;
    AssetLoader* asset_loader_ = nullptr;
    MotionConverter* motion_converter_ = nullptr;
    QWidget* viewer_container_ = nullptr;
    MotionBrowserPanel* browser_panel_ = nullptr;
    SceneViewport* simulation_viewport_ = nullptr;
    std::unique_ptr<AppController> simulation_controller_;
};
