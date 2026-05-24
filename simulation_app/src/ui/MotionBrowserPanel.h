#pragma once

#include "io/MotionAsset.h"

#include <functional>
#include <filesystem>
#include <vector>

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

class MotionBrowserPanel final : public QWidget {
public:
    explicit MotionBrowserPanel(QWidget* parent = nullptr);
    bool is_expanded() const;

    void set_motion_list(std::vector<MotionAsset> motions);
    void set_current_motion_asset(const std::filesystem::path& motion_asset_path);
    void set_motion_selected_callback(std::function<void(const std::filesystem::path&)> callback);

    void set_conversion_active(bool active);
    void set_motion_import_button_callback(std::function<void()> callback);
    void set_garment_button_callback(std::function<void()> callback);
    void set_expansion_changed_callback(std::function<void()> callback);

private:
    void set_expanded(bool expanded);
    void update_expanded_state();

    void rebuild_list();
    void select_motion_from_list(QListWidgetItem* item);

    QPushButton* toggle_button_ = nullptr;
    QPushButton* garment_button_ = nullptr;
    QWidget* expanded_panel_ = nullptr;
    QLabel* title_label_ = nullptr;
    QListWidget* list_widget_ = nullptr;
    QPushButton* import_button_ = nullptr;
    bool expanded_ = false;
    std::vector<MotionAsset> motions_;
    std::function<void(const std::filesystem::path&)> motion_selected_callback_;
    std::function<void()> motion_import_button_callback_;
    std::function<void()> garment_button_callback_;
    std::function<void()> expansion_changed_callback_;
};
