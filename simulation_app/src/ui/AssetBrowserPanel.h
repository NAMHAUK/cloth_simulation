#pragma once

#include <functional>
#include <filesystem>
#include <vector>

#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class ElidedLabel;
class QPushButton;
class QTableWidget;
class QWidget;

enum class AssetPanelMode {
    Motions,
    Garments,
};

class AssetBrowserPanel final : public QWidget {
public:
    explicit AssetBrowserPanel(const std::filesystem::path& motion_catalog_path,
                               const std::filesystem::path& subject_catalog_path,
                               QWidget* parent = nullptr);
    bool is_expanded() const;

    void set_motion_paths(std::vector<std::filesystem::path> source_paths,
                          std::vector<std::filesystem::path> asset_paths);
    void set_garment_paths(std::vector<std::filesystem::path> asset_paths);
    void set_selected_callback(std::function<void(AssetPanelMode, const std::filesystem::path&)> callback);
    void set_motion_conversion_callback(std::function<void(const std::filesystem::path&)> callback);
    void set_motion_selection_enabled(bool enabled);
    void set_garment_selection_enabled(bool enabled);

    void set_conversion_active(AssetPanelMode mode, bool active);
    void set_import_button_callback(std::function<void()> callback);
    void set_expansion_changed_callback(std::function<void()> callback);

private:
    void set_expanded(bool expanded);
    void open_list(AssetPanelMode mode);
    void update_expanded_state();

    void rebuild_list();
    void rebuild_subject_list();
    void rebuild_motion_list();
    void rebuild_garment_list();
    void select_table_row(int row);
    void update_import_button_state();

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
    std::function<void(AssetPanelMode, const std::filesystem::path&)> selected_callback_;
    std::function<void(const std::filesystem::path&)> motion_conversion_callback_;
    std::function<void()> import_button_callback_;
    std::function<void()> expansion_changed_callback_;
};
