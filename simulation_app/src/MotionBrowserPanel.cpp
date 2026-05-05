#include "MotionBrowserPanel.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

MotionBrowserPanel::MotionBrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    toggleButton_ = new QPushButton("Motions", this);
    toggleButton_->setCheckable(true);
    toggleButton_->setMinimumHeight(32);
    toggleButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toggleButton_->setStyleSheet(
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

    expandedPanel_ = new QWidget(this);
    expandedPanel_->setStyleSheet(
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

    auto* panelLayout = new QVBoxLayout(expandedPanel_);
    panelLayout->setContentsMargins(10, 10, 10, 10);
    panelLayout->setSpacing(8);

    auto* titleLabel = new QLabel("Motion Cache", expandedPanel_);
    titleLabel->setStyleSheet("font-weight: 600;");

    statusLabel_ = new QLabel("No cache found", expandedPanel_);
    statusLabel_->setStyleSheet("color: #666;");

    listWidget_ = new QListWidget(expandedPanel_);
    listWidget_->setSelectionMode(QAbstractItemView::SingleSelection);

    importButton_ = new QPushButton("+", expandedPanel_);
    importButton_->setMinimumHeight(32);

    layout->addWidget(toggleButton_, 0, Qt::AlignLeft);
    layout->addWidget(expandedPanel_, 1);

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(statusLabel_);
    panelLayout->addWidget(listWidget_, 1);
    panelLayout->addWidget(importButton_);

    connect(toggleButton_, &QPushButton::clicked, this, [this]() {
        setExpanded(!expanded_);
    });

    connect(listWidget_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < static_cast<int>(motions_.size()) && motionSelectedCallback_) {
            motionSelectedCallback_(motions_[static_cast<std::size_t>(index)].cachePath);
        }
    });

    connect(importButton_, &QPushButton::clicked, this, [this]() {
        if (importRequestedCallback_) {
            importRequestedCallback_();
        }
    });

    updateExpandedState();
}

void MotionBrowserPanel::setMotions(std::vector<MotionEntry> motions)
{
    motions_ = std::move(motions);
    rebuildList();
}

void MotionBrowserPanel::setCurrentCache(const std::filesystem::path& cachePath)
{
    for (int i = 0; i < static_cast<int>(motions_.size()); ++i) {
        if (motions_[static_cast<std::size_t>(i)].cachePath == cachePath) {
            listWidget_->setCurrentRow(i);
            return;
        }
    }
}

void MotionBrowserPanel::setMotionSelectedCallback(
    std::function<void(const std::filesystem::path&)> callback)
{
    motionSelectedCallback_ = std::move(callback);
}

void MotionBrowserPanel::setImportRequestedCallback(std::function<void()> callback)
{
    importRequestedCallback_ = std::move(callback);
}

void MotionBrowserPanel::setExpansionChangedCallback(std::function<void()> callback)
{
    expansionChangedCallback_ = std::move(callback);
}

bool MotionBrowserPanel::isExpanded() const
{
    return expanded_;
}

void MotionBrowserPanel::rebuildList()
{
    listWidget_->clear();
    statusLabel_->setText(QString("%1 cache file(s)").arg(static_cast<int>(motions_.size())));

    for (int i = 0; i < static_cast<int>(motions_.size()); ++i) {
        const MotionEntry& motion = motions_[static_cast<std::size_t>(i)];
        auto* item = new QListWidgetItem(QString::fromStdString(motion.displayName), listWidget_);
        item->setData(Qt::UserRole, i);
        item->setToolTip(QString::fromStdWString(motion.cachePath.wstring()));
    }
}

void MotionBrowserPanel::setExpanded(bool expanded)
{
    if (expanded_ == expanded) {
        return;
    }

    expanded_ = expanded;
    updateExpandedState();

    if (expansionChangedCallback_) {
        expansionChangedCallback_();
    }
}

void MotionBrowserPanel::updateExpandedState()
{
    toggleButton_->setChecked(expanded_);
    toggleButton_->setText("Motions");
    expandedPanel_->setVisible(expanded_);
}
