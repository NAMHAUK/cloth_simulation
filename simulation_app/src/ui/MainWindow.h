#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include <glm/vec3.hpp>

#include <QMainWindow>

#include "app/ProjectPaths.h"
#include "asset/AssetConverter.h"
#include "asset/AssetDataTypes.h"

class AssetBrowserPanel;
class AssetLoader;
class GarmentColorPanel;
class GarmentPlacementPanel;
class QFrame;
class QLabel;
class QObject;
class QString;
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
    enum class PlacementGroupState
    {
        Hidden,
        Empty,
        Loaded,
    };

    struct GarmentCard final
    {
        glm::vec3 color{1.0f};
        bool has_garment = false;
        bool is_confirmed = false;
        QFrame* frame = nullptr;
        QPushButton* color_button = nullptr;
        QLabel* name_label = nullptr;
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
    std::optional<GarmentLayer> take_garment_request_layer(std::uint64_t request_id);
    bool has_pending_garment_load() const;
    bool has_visible_placement_group() const;
    bool has_placement_session() const;
    void update_placement_actions();
    void end_placement_session();
    void create_garment_cards();
    void set_garment_card(GarmentLayer layer, const QString& garment_name, const glm::vec3& color);
    void update_garment_card_color(GarmentLayer layer, const glm::vec3& color);
    void update_garment_cards();
    void clear_garment_card(GarmentLayer layer);
    void clear_placement_garment_cards();
    void highlight_color_edit_card(std::optional<GarmentLayer> layer);
    void choose_garment_color(GarmentLayer layer);
    void refresh_motion_list();
    void refresh_garment_list();

    void request_garment_conversion();
    void request_motion_conversion(const std::filesystem::path& source_path);
    std::optional<ConverterCommand> prepare_garment_conversion();

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    AssetConverter* motion_converter_ = nullptr;
    AssetConverter* garment_converter_ = nullptr;
    QWidget* viewer_container_ = nullptr;
    AssetBrowserPanel* browser_panel_ = nullptr;
    GarmentPlacementPanel* garment_placement_panel_ = nullptr;
    GarmentColorPanel* garment_color_panel_ = nullptr;
    QWidget* garment_cards_panel_ = nullptr;
    QPushButton* run_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    SceneViewport* simulation_viewport_ = nullptr;
    std::unique_ptr<SimulationController> simulation_controller_;
    std::array<PlacementGroupState, 2> placement_group_states_{};
    std::array<std::optional<std::uint64_t>, 2> garment_request_ids_{};
    std::array<GarmentCard, 2> garment_cards_{};
    std::optional<GarmentLayer> color_edit_card_layer_;
};
