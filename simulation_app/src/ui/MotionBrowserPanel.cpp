#include "ui/MotionBrowserPanel.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

// UI 생성과 표시 상태 //

MotionBrowserPanel::MotionBrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);

    // 토글 버튼 생성
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
    );

    garment_button_ = new QPushButton("Garment", this);
    garment_button_->setMinimumHeight(32);
    garment_button_->setMinimumWidth(86);
    garment_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    garment_button_->setStyleSheet(toggle_button_->styleSheet());

    // 토글 열었을 때 panel 생성
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

    // expanded panel 내부 구성 설정
    auto* panel_layout = new QVBoxLayout(expanded_panel_);
    panel_layout->setContentsMargins(10, 10, 10, 10);
    panel_layout->setSpacing(8);

    title_label_ = new QLabel("사용가능한 motions (0)", expanded_panel_);
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

    // 각 버튼 callback 설정
    connect(toggle_button_, &QPushButton::clicked, this, [this]() {
        set_expanded(!expanded_);
    });

    connect(garment_button_, &QPushButton::clicked, this, [this]() {
        if (garment_button_callback_) {
            garment_button_callback_();
        }
    });

    connect(list_widget_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        select_motion_from_list(item);
    });

    connect(import_button_, &QPushButton::clicked, this, [this]() {
        if (motion_import_button_callback_) {
            motion_import_button_callback_();
        }
    });

    update_expanded_state();
}

bool MotionBrowserPanel::is_expanded() const
{
    return expanded_;
}

void MotionBrowserPanel::set_expanded(bool expanded)
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

void MotionBrowserPanel::update_expanded_state()
{
    toggle_button_->setChecked(expanded_);
    toggle_button_->setText("Motions");
    expanded_panel_->setVisible(expanded_);
}


// Motion 목록 표시 & 선택 //

// 표시할 motion list set
void MotionBrowserPanel::set_motion_list(std::vector<MotionAsset> motions)
{
    motions_ = std::move(motions);
    rebuild_list();
}
// motion asset list 새로고침
void MotionBrowserPanel::rebuild_list()
{
    list_widget_->clear();
    title_label_->setText(QString("사용가능한 motions (%1)").arg(static_cast<int>(motions_.size())));

    for (int i = 0; i < static_cast<int>(motions_.size()); ++i) {
        const MotionAsset& motion = motions_[static_cast<std::size_t>(i)];
        auto* item = new QListWidgetItem(QString::fromStdString(motion.display_name), list_widget_);
        item->setData(Qt::UserRole, i);
        item->setToolTip(QString::fromStdWString(motion.motion_asset_path.wstring()));
    }
}
// 현재 선택된 motion asset을 list에서 선택된 상태로 표시
void MotionBrowserPanel::set_current_motion_asset(const std::filesystem::path& motion_asset_path)
{
    for (int i = 0; i < static_cast<int>(motions_.size()); ++i) {
        if (motions_[static_cast<std::size_t>(i)].motion_asset_path == motion_asset_path) {
            list_widget_->setCurrentRow(i);
            return;
        }
    }
}
// 변환 버튼 활성/비활성 설정
void MotionBrowserPanel::set_conversion_active(bool active)
{
    import_button_->setEnabled(!active);
}


// 외부 callback 설정 //

// motion 선택 시 발생하는 callback 함수 등록
void MotionBrowserPanel::set_motion_selected_callback(
    std::function<void(const std::filesystem::path&)> callback)
{
    motion_selected_callback_ = std::move(callback);
}

// list에서 motion이 클릭 됐을 때 발생할 함수
// 선택된 motion의 motion_asset_path로 set_motion_selected_callback으로 등록된 callback 함수 호출
void MotionBrowserPanel::select_motion_from_list(QListWidgetItem* item)
{
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < static_cast<int>(motions_.size()) && motion_selected_callback_) {
        motion_selected_callback_(motions_[static_cast<std::size_t>(index)].motion_asset_path);
    }
}

void MotionBrowserPanel::set_motion_import_button_callback(std::function<void()> callback)
{
    motion_import_button_callback_ = std::move(callback);
}

void MotionBrowserPanel::set_expansion_changed_callback(std::function<void()> callback)
{
    expansion_changed_callback_ = std::move(callback);
}

void MotionBrowserPanel::set_garment_button_callback(std::function<void()> callback)
{
    garment_button_callback_ = std::move(callback);
}
