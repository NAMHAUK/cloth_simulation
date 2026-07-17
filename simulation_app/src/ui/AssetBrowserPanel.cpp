#include "ui/AssetBrowserPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>
namespace {
QString make_asset_display_name(const std::filesystem::path& asset_path)
{
    const std::filesystem::path stem = asset_path.stem();
    const std::filesystem::path display_path = stem.empty() ? asset_path.filename() : stem;
    return QString::fromStdWString(display_path.wstring());
}
}

AssetBrowserPanel::AssetBrowserPanel(QWidget* parent): QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);

    toggle_button_ = new QPushButton("Motions", this);
    toggle_button_->setCheckable(true);
    toggle_button_->setMinimumHeight(32);
    toggle_button_->setMinimumWidth(86);
    toggle_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toggle_button_->setStyleSheet(
        "QPushButton {"
        "  background-color: #3a3a3a;"
        "  color: white;"
        "  border: 1px solid #242424;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:checked {"
        "  background-color: #2f2f2f;"
        "}"
        "QPushButton:hover {"
        "  background-color: #4a4a4a;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #555555;"
        "  color: #bdbdbd;"
        "  border-color: #444444;"
        "}"
    );

    garment_button_ = new QPushButton("Garment", this);
    garment_button_->setCheckable(true);
    garment_button_->setMinimumHeight(32);
    garment_button_->setMinimumWidth(86);
    garment_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    garment_button_->setStyleSheet(toggle_button_->styleSheet());

    expanded_panel_ = new QWidget(this);
    expanded_panel_->setStyleSheet(
        "QWidget {"
        "  background-color: rgba(245, 245, 245, 235);"
        "  border: 1px solid #9a9a9a;"
        "  border-radius: 4px;"
        "}"
        "QLabel {"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QListWidget {"
        "  background-color: white;"
        "  border: 1px solid #b5b5b5;"
        "}"
        "QPushButton {"
        "  background-color: #3a3a3a;"
        "  color: white;"
        "  border: 1px solid #242424;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #4a4a4a;"
        "}"
    );

    auto* panel_layout = new QVBoxLayout(expanded_panel_);
    panel_layout->setContentsMargins(10, 10, 10, 10);
    panel_layout->setSpacing(8);

    title_label_ = new QLabel("Available motions (0)", expanded_panel_);
    title_label_->setStyleSheet("font-weight: 600;");

    list_widget_ = new QListWidget(expanded_panel_);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    import_button_ = new QPushButton("+", expanded_panel_);
    import_button_->setMinimumHeight(32);

    button_layout->addWidget(toggle_button_);
    button_layout->addWidget(garment_button_);
    button_layout->addStretch(1);

    layout->addLayout(button_layout);
    layout->addWidget(expanded_panel_, 1);

    panel_layout->addWidget(title_label_);
    panel_layout->addWidget(list_widget_, 1);
    panel_layout->addWidget(import_button_);

    connect(toggle_button_, &QPushButton::clicked, this, [this]() {
        if (expanded_ && panel_mode_ == AssetPanelMode::Motions) {
            set_expanded(false);
            return;
        }
        open_list(AssetPanelMode::Motions);
    });

    connect(garment_button_, &QPushButton::clicked, this, [this]() {
        if (expanded_ && panel_mode_ == AssetPanelMode::Garments) {
            set_expanded(false);
            return;
        }
        open_list(AssetPanelMode::Garments);
    });

    connect(list_widget_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        select_asset_from_list(item);
    });

    connect(import_button_, &QPushButton::clicked, this, [this]() {
        if (import_button_callback_) {
            import_button_callback_(panel_mode_);
        }
    });

    update_expanded_state();
}

// panel 상태 전환 //
// open_list -> update_expanded_state
void AssetBrowserPanel::open_list(AssetPanelMode mode)
{
    panel_mode_ = mode;
    rebuild_list();

    if (expanded_) {
        update_expanded_state();
    }else{
        set_expanded(true);
    }
}

void AssetBrowserPanel::update_expanded_state()
{
    toggle_button_->setChecked(expanded_ && panel_mode_ == AssetPanelMode::Motions);
    garment_button_->setChecked(expanded_ && panel_mode_ == AssetPanelMode::Garments);
    expanded_panel_->setVisible(expanded_);
}

void AssetBrowserPanel::set_expanded(bool expanded)
{
    if (expanded_ == expanded) {
        return;
    }

    expanded_ = expanded;
    update_expanded_state();

    if (expansion_changed_callback_) {
        expansion_changed_callback_();
    }
}

void AssetBrowserPanel::set_expansion_changed_callback(std::function<void()> callback)
{
    expansion_changed_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_garment_selection_enabled(bool enabled)
{
    if (!enabled && expanded_ && panel_mode_ == AssetPanelMode::Garments) {
        set_expanded(false);
    }
    garment_button_->setEnabled(enabled);
    garment_button_->setToolTip(enabled ? "" : "Up to two garments can be added.");
}

// list 표시 //
void AssetBrowserPanel::set_asset_paths(AssetPanelMode mode, std::vector<std::filesystem::path> asset_paths)
{
    asset_paths_for(mode) = std::move(asset_paths);
    rebuild_list();
}

void AssetBrowserPanel::rebuild_list()
{
    list_widget_->clear();

    const auto& current_asset_paths = asset_paths_for(panel_mode_);
    const char* title = panel_mode_ == AssetPanelMode::Motions ? "Available motions (%1)" : "Available garments (%1)";
    title_label_->setText(QString(title).arg(static_cast<int>(current_asset_paths.size())));

    // list에 각 asset의 이름 추가
    for (const auto& asset_path : current_asset_paths) {
        const auto full_path = QString::fromStdWString(asset_path.wstring());

        auto* item = new QListWidgetItem(make_asset_display_name(asset_path), list_widget_);
        item->setData(Qt::UserRole, full_path);
        item->setToolTip(full_path);
    }
}

void AssetBrowserPanel::set_selected_callback(std::function<void(AssetPanelMode, const std::filesystem::path&)> callback)
{
    selected_callback_ = std::move(callback);
}

void AssetBrowserPanel::select_asset_from_list(QListWidgetItem* item)
{
    if (selected_callback_) {
        const auto asset_path = item->data(Qt::UserRole).toString().toStdWString();
        selected_callback_(panel_mode_, asset_path);
    }
}

// add button //
void AssetBrowserPanel::set_conversion_active(AssetPanelMode mode, bool active)
{
    if (mode == AssetPanelMode::Motions) {
        motion_conversion_active_ = active;
    } else {
        garment_conversion_active_ = active;
    }
    update_import_button_state();
}

void AssetBrowserPanel::set_import_button_callback(std::function<void(AssetPanelMode)> callback)
{
    import_button_callback_ = std::move(callback);
}

void AssetBrowserPanel::update_import_button_state()
{
    const bool conversion_active = panel_mode_ == AssetPanelMode::Motions
        ? motion_conversion_active_
        : garment_conversion_active_;
    import_button_->setEnabled(!conversion_active);
}

// getter //
bool AssetBrowserPanel::is_expanded() const
{
    return expanded_;
}

std::vector<std::filesystem::path>& AssetBrowserPanel::asset_paths_for(AssetPanelMode mode)
{
    return mode == AssetPanelMode::Motions ? motion_asset_paths_ : garment_asset_paths_;
}

const std::vector<std::filesystem::path>& AssetBrowserPanel::asset_paths_for(AssetPanelMode mode) const
{
    return mode == AssetPanelMode::Motions ? motion_asset_paths_ : garment_asset_paths_;
}
