#pragma once

#include "app/ProjectPaths.h"
#include "asset/AssetDataTypes.h"

#include <filesystem>
#include <functional>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class ElidedLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
class QWidget;
class AssetConverter;
class AssetLoader;

class AssetBrowserPanel final : public QWidget
{
    Q_OBJECT

public:
    using MotionLoadedCallback = std::function<void(CharacterMotion)>;
    using GarmentLoadedCallback =
        std::function<void(const std::filesystem::path& asset_path, GarmentMesh mesh)>;

    explicit AssetBrowserPanel(const ProjectPaths& project_paths, QWidget* parent = nullptr);
    ~AssetBrowserPanel() override;

    void set_motion_selection_enabled(bool enabled);
    void set_garment_selection_enabled(bool enabled);

    bool is_expanded() const;

    void set_motion_loaded_callback(MotionLoadedCallback callback);
    void set_garment_loaded_callback(GarmentLoadedCallback callback);

Q_SIGNALS:
    void motion_loading_changed(bool is_loading);
    void expansion_changed();

private:
    enum class State
    {
        Closed,
        Subjects,
        Motions,
        Garments,
    };

    struct MotionSubject
    {
        int id;
        std::vector<std::filesystem::path> motion_paths;
    };

    void setup_asset_buttons(QVBoxLayout& root_layout);
    void setup_list_panel(QVBoxLayout& root_layout);
    void setup_asset_loader();
    void setup_asset_converters();
    void load_motion_paths();

    void toggle_motion_list();
    void toggle_garment_list();
    void return_to_subject_list();
    void handle_table_row_click(int row);
    void set_state(State state);

    void refresh_garment_list();
    void load_motion(const std::filesystem::path& asset_path);
    void load_garment(const std::filesystem::path& asset_path);
    void request_garment_conversion();
    void request_motion_conversion(const std::filesystem::path& source_path);
    void finish_motion_conversion();

    void rebuild_list();
    void rebuild_subject_list();
    void rebuild_motion_list();
    void set_motion_row(int row,
                        const std::filesystem::path& source_path,
                        const QString& converting_motion_id);
    void rebuild_garment_list();

    ProjectPaths project_paths_;
    AssetLoader* asset_loader_ = nullptr;
    AssetConverter* motion_converter_ = nullptr;
    AssetConverter* garment_converter_ = nullptr;
    QPushButton* motion_button_ = nullptr;
    QPushButton* garment_button_ = nullptr;
    QWidget* list_panel_ = nullptr;
    QPushButton* subject_back_button_ = nullptr;
    ElidedLabel* title_label_ = nullptr;
    QTableWidget* table_widget_ = nullptr;
    QPushButton* import_button_ = nullptr;
    State state_ = State::Closed;
    int selected_subject_index_ = 0;
    std::filesystem::path converting_motion_asset_path_;
    std::vector<MotionSubject> subjects_;
    std::vector<std::filesystem::path> garment_asset_paths_;
    QHash<QString, QString> converted_motion_paths_;
    QJsonObject motion_descriptions_;
    QJsonObject subject_descriptions_;
    MotionLoadedCallback motion_loaded_callback_;
    GarmentLoadedCallback garment_loaded_callback_;
};
