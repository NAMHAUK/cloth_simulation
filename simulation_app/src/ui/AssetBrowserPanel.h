#pragma once

#include "app/ProjectPaths.h"
#include "asset/AssetDataTypes.h"

#include <filesystem>
#include <functional>
#include <vector>

#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class ElidedLabel;
class QPushButton;
class QTableWidget;
class QWidget;
class AssetConverter;
class AssetLoader;

enum class AssetPanelMode
{
    Motions,
    Garments,
};

class AssetBrowserPanel final : public QWidget
{
public:
    using MotionLoadingChangedCallback = std::function<void(bool)>;
    using MotionLoadedCallback = std::function<void(CharacterMesh)>;
    using GarmentLoadStartedCallback = std::function<void()>;
    using GarmentLoadedCallback =
        std::function<void(const std::filesystem::path& asset_path, GarmentMesh mesh)>;

    explicit AssetBrowserPanel(const ProjectPaths& project_paths, QWidget* parent = nullptr);
    ~AssetBrowserPanel() override;

    bool is_expanded() const;

    void set_motion_loading_changed_callback(MotionLoadingChangedCallback callback);
    void set_motion_loaded_callback(MotionLoadedCallback callback);
    void set_garment_load_started_callback(GarmentLoadStartedCallback callback);
    void set_garment_loaded_callback(GarmentLoadedCallback callback);
    void set_motion_selection_enabled(bool enabled);
    void set_garment_selection_enabled(bool enabled);
    void set_expansion_changed_callback(std::function<void()> callback);

private:
    // Asset operations
    void refresh_motion_list();
    void refresh_garment_list();
    void load_motion(const std::filesystem::path& asset_path);
    void load_garment(const std::filesystem::path& asset_path);
    void request_garment_conversion();
    void request_motion_conversion(const std::filesystem::path& source_path);
    void set_conversion_active(AssetPanelMode mode, bool active);

    // Panel state
    void set_expanded(bool expanded);
    void open_list(AssetPanelMode mode);
    void update_expanded_state();
    void set_motion_paths(std::vector<std::filesystem::path> source_paths,
                          std::vector<std::filesystem::path> asset_paths);
    void set_garment_paths(std::vector<std::filesystem::path> asset_paths);

    // List display
    void rebuild_list();
    void rebuild_subject_list();
    void rebuild_motion_list();
    void rebuild_garment_list();
    void select_table_row(int row);
    void update_import_button_state();

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    AssetConverter* motion_converter_ = nullptr;
    AssetConverter* garment_converter_ = nullptr;
    QPushButton* toggle_button_ = nullptr;
    QPushButton* garment_button_ = nullptr;
    QWidget* expanded_panel_ = nullptr;
    QPushButton* subject_back_button_ = nullptr;
    ElidedLabel* title_label_ = nullptr;
    QTableWidget* table_widget_ = nullptr;
    QPushButton* import_button_ = nullptr;
    AssetPanelMode panel_mode_ = AssetPanelMode::Motions;
    bool expanded_ = false;
    bool motion_conversion_active_ = false;
    bool garment_conversion_active_ = false;
    QString selected_subject_id_;
    QString converting_motion_path_;
    std::vector<std::filesystem::path> motion_source_paths_;
    std::vector<std::filesystem::path> garment_asset_paths_;
    QHash<QString, QString> converted_motion_paths_;
    QHash<QString, QString> motion_descriptions_;
    QHash<QString, QString> subject_descriptions_;
    MotionLoadingChangedCallback motion_loading_changed_callback_;
    MotionLoadedCallback motion_loaded_callback_;
    GarmentLoadStartedCallback garment_load_started_callback_;
    GarmentLoadedCallback garment_loaded_callback_;
    std::function<void()> expansion_changed_callback_;
};
