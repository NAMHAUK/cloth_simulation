#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include <QMainWindow>

#include "app/ProjectPaths.h"
#include "asset/AssetConverter.h"

class AssetBrowserPanel;
class AssetLoader;
class GarmentPlacementPanel;
class QObject;
class SimulationController;
class SceneViewport;
class QEvent;
class QOpenGLFunctions_4_5_Core;
class QPushButton;
class QWidget;
enum class AssetPanelMode;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class PlacementGroupState {
        Hidden,
        Empty,
        Loaded,
    };

    bool initialize_scene(QOpenGLFunctions_4_5_Core& gl);

    void setup_callbacks();
    void setup_viewport_callbacks();
    void setup_browser_callbacks();
    void setup_asset_loader_callbacks();
    void setup_asset_converter_callbacks();

    void update_viewer_layout();
    void update_simulation_controls();
    void request_garment_load(const std::filesystem::path& asset_path);
    std::optional<std::size_t> take_garment_request_group(std::uint64_t request_id);
    bool has_pending_garment_load() const;
    bool has_visible_placement_group() const;
    bool has_placement_session() const;
    void update_placement_actions();
    void end_placement_session();
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
    GarmentPlacementPanel* garment_placement_panel_ = nullptr;
    QPushButton* run_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    SceneViewport* simulation_viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
    std::array<PlacementGroupState, 2> placement_group_states_{};
    std::array<std::optional<std::uint64_t>, 2> garment_request_ids_{};
};
