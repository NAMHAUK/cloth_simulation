#include "ui/MotionCard.h"

#include "ui/ElidedLabel.h"

#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

MotionCard::MotionCard(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(88);
    hide();

    button_ = new QPushButton(this);
    button_->setObjectName("motionCard");
    button_->setCursor(Qt::PointingHandCursor);

    auto* title = new QLabel("motion convert complete", button_);
    title->setObjectName("motionCardTitle");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    name_label_ = new ElidedLabel(QString{}, button_);
    name_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    name_label_->setTextFormat(Qt::PlainText);
    name_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* card_layout = new QVBoxLayout(button_);
    card_layout->setContentsMargins(16, 12, 16, 12);
    card_layout->setSpacing(8);
    card_layout->addWidget(title);
    card_layout->addWidget(name_label_);

    animation_ = new QPropertyAnimation(button_, "pos", this);
    animation_->setDuration(300);
    animation_->setEndValue(QPoint{0, 0});
    animation_->setEasingCurve(QEasingCurve::OutCubic);

    connect(button_, &QPushButton::clicked, this, [this]() { Q_EMIT motion_activated(asset_path_); });
}

void MotionCard::show_motion(const std::filesystem::path& asset_path,
                             const QString& motion_id,
                             const QString& description)
{
    animation_->stop();
    asset_path_ = asset_path;
    const QString text = motion_id + " " + description;
    name_label_->set_text(text);
    button_->setAccessibleName("motion convert complete " + text);
    button_->setToolTip("motion convert complete\n" + text);
    show();

    Q_EMIT layout_changed();
    animation_->setStartValue(QPoint{-width(), 0});
    animation_->start();
}

void MotionCard::dismiss(const std::filesystem::path& asset_path)
{
    if (asset_path != asset_path_) {
        return;
    }

    asset_path_.clear();
    animation_->stop();
    hide();
    Q_EMIT layout_changed();
}

void MotionCard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    button_->resize(size());
}
