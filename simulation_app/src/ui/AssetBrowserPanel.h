#pragma once

#include <functional>
#include <filesystem>
#include <vector>

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

enum class AssetPanelMode {
    Motions,
    Garments,
};

class AssetBrowserPanel final : public QWidget {
public:
    explicit AssetBrowserPanel(QWidget* parent = nullptr);
    bool is_expanded() const;

    void set_asset_paths(AssetPanelMode mode, std::vector<std::filesystem::path> asset_paths);
    void set_selected_callback(std::function<void(AssetPanelMode, const std::filesystem::path&)> callback);
    void set_garment_selection_enabled(bool enabled);

    void set_conversion_active(AssetPanelMode mode, bool active);
    void set_import_button_callback(std::function<void(AssetPanelMode)> callback);
    void set_expansion_changed_callback(std::function<void()> callback);

private:
    void set_expanded(bool expanded);
    void open_list(AssetPanelMode mode);
    void update_expanded_state();

    void rebuild_list();
    void select_asset_from_list(QListWidgetItem* item);
    void update_import_button_state();
    std::vector<std::filesystem::path>& asset_paths_for(AssetPanelMode mode);
    const std::vector<std::filesystem::path>& asset_paths_for(AssetPanelMode mode) const;

    QPushButton* toggle_button_ = nullptr;
    QPushButton* garment_button_ = nullptr;
    QWidget* expanded_panel_ = nullptr;
    QLabel* title_label_ = nullptr;
    QListWidget* list_widget_ = nullptr;
    QPushButton* import_button_ = nullptr;
    AssetPanelMode panel_mode_ = AssetPanelMode::Motions;
    bool expanded_ = false;
    bool motion_conversion_active_ = false;
    bool garment_conversion_active_ = false;
    std::vector<std::filesystem::path> motion_asset_paths_;
    std::vector<std::filesystem::path> garment_asset_paths_;
    std::function<void(AssetPanelMode, const std::filesystem::path&)> selected_callback_;
    std::function<void(AssetPanelMode)> import_button_callback_;
    std::function<void()> expansion_changed_callback_;
};
