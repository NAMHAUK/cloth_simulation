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
#include "asset/AssetDataTypes.h"

class AssetBrowserPanel;
class AssetLoader;
class GarmentColorPanel;
class GarmentCardsPanel;
class GarmentPlacementPanel;
class QObject;
class SimulationController;
class SceneViewport;
class QEvent;
class QOpenGLFunctions_4_5_Core;
class QPushButton;
class QWidget;
enum class AssetPanelMode;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class PlacementPhase
    {
        Hidden,
        Waiting,
        Ready,
    };

    // Initialization
    bool initialize_scene(QOpenGLFunctions_4_5_Core& gl);
    void setup_viewport_callbacks();
    void setup_asset_browser_callbacks();
    void setup_garment_placement_callbacks();
    void setup_garment_color_callbacks();
    void setup_simulation_control_callbacks();
    void setup_asset_loader_callbacks();
    void setup_asset_converter_callbacks();

    // Garment placement
    void request_garment_load(const std::filesystem::path& asset_path);
    void end_placement_session();
    void clear_placement_garment_cards();
    std::optional<GarmentLayer> take_garment_layer(std::uint64_t request_id);
    bool has_placement_session() const;
    bool has_visible_placement_group() const;
    bool has_pending_garment_load() const;

    // UI updates
    void update_simulation_controls();
    void update_placement_button_state();
    void update_viewer_layout();
    void refresh_motion_list();
    void refresh_garment_list();

    // Asset conversion
    std::optional<ConverterCommand> prepare_garment_conversion();
    void request_garment_conversion();
    void request_motion_conversion(const std::filesystem::path& source_path);

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    AssetConverter* motion_converter_ = nullptr;
    AssetConverter* garment_converter_ = nullptr;
    QWidget* viewer_container_ = nullptr;
    AssetBrowserPanel* asset_browser_panel_ = nullptr;
    GarmentPlacementPanel* garment_placement_panel_ = nullptr;
    GarmentColorPanel* garment_color_panel_ = nullptr;
    GarmentCardsPanel* garment_cards_panel_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* default_pose_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    SceneViewport* viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
    std::array<PlacementPhase, 2> placement_phases_{};
    std::array<std::optional<std::uint64_t>, 2> garment_request_ids_{};
};
