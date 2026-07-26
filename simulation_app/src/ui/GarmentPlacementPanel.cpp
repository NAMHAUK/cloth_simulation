#include "ui/GarmentPlacementPanel.h"

#include <array>
#include <cstddef>
#include <utility>

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include <QtColorWidgets/color_2d_slider.hpp>

namespace {
constexpr int position_slider_min = -30;
constexpr int position_slider_max = 30;
constexpr int position_slider_center = 0;
constexpr float position_slider_factor = 0.01f;
constexpr int scale_slider_min = 50;
constexpr int scale_slider_max = 150;
constexpr int scale_slider_center = 100;
constexpr float scale_slider_factor = 0.01f;
constexpr int hue_slider_max = 359;
constexpr int placement_button_size = 26;
constexpr int close_icon_size = 12;
constexpr int action_icon_size = 20;
constexpr int add_button_height = 30;
constexpr int confirm_icon_size = 28;

class AbsoluteSliderStyle final : public QProxyStyle
{
public:
    int styleHint(
        StyleHint hint,
        const QStyleOption* option,
        const QWidget* widget,
        QStyleHintReturn* return_data
    ) const override
    {
        if (hint == SH_Slider_AbsoluteSetButtons) {
            return Qt::LeftButton;
        }
        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }
};

QString format_float(float value)
{
    return QString::number(value, 'f', 2);
}

QIcon make_close_icon()
{
    QPixmap pixmap(close_icon_size, close_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(2, 2, close_icon_size - 2, close_icon_size - 2);
    painter.drawLine(close_icon_size - 2, 2, 2, close_icon_size - 2);
    return QIcon{pixmap};
}

QIcon make_plus_icon()
{
    QPixmap pixmap(action_icon_size, action_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(4, action_icon_size / 2, action_icon_size - 4, action_icon_size / 2);
    painter.drawLine(action_icon_size / 2, 4, action_icon_size / 2, action_icon_size - 4);
    return QIcon{pixmap};
}

QIcon make_confirm_icon()
{
    QPixmap pixmap(confirm_icon_size, confirm_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{QColor{"#2e7d32"}, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
    painter.drawLine(QPointF{4.0, 15.0}, QPointF{10.0, 21.0});
    painter.drawLine(QPointF{10.0, 21.0}, QPointF{24.0, 7.0});
    return QIcon{pixmap};
}
}

GarmentPlacementPanel::GarmentPlacementPanel(QWidget* parent): QWidget(parent)
{
    setObjectName("garmentPlacementPanel");
    setStyleSheet(
        "#garmentPlacementPanel {"
        "  background-color: rgba(245, 245, 245, 235);"
        "  border: 1px solid #9a9a9a;"
        "  border-radius: 4px;"
        "}"
        "#garmentPlacementPanel QLabel {"
        "  border: none;"
        "  background: transparent;"
        "}"
        "#garmentPlacementPanel QPushButton {"
        "  background-color: #1f6feb;"
        "  color: white;"
        "  border: 1px solid #1158c7;"
        "  border-radius: 4px;"
        "  padding: 2px 8px;"
        "}"
        "#garmentPlacementPanel QPushButton:hover {"
        "  background-color: #2f81f7;"
        "}"
        "#garmentPlacementPanel QPushButton:disabled {"
        "  background-color: #8caee6;"
        "  color: #dddddd;"
        "}"
        "#garmentPlacementPanel QSlider {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#placementGroup {"
        "  background-color: rgba(255, 255, 255, 150);"
        "  border: 1px solid #b8b8b8;"
        "  border-radius: 4px;"
        "}"
        "#placementGroup[active=\"true\"] {"
        "  border: 2px solid #1f6feb;"
        "}"
        "#garmentPlacementPanel #placementCancelButton {"
        "  padding: 0px;"
        "  background-color: #5a5a5a;"
        "  border: 1px solid #242424;"
        "  border-radius: 4px;"
        "}"
        "#garmentPlacementPanel #placementCancelButton:hover {"
        "  background-color: #7a7a7a;"
        "}"
        "#garmentPlacementPanel #upperRemoveButton {"
        "  padding: 0px;"
        "  background-color: #1f6feb;"
        "  border: 1px solid #1158c7;"
        "  border-radius: 4px;"
        "}"
        "#garmentPlacementPanel #upperRemoveButton:hover {"
        "  background-color: #2f81f7;"
        "}"
        "#garmentPlacementPanel #addUpperButton {"
        "  padding: 0;"
        "}"
        "#garmentPlacementPanel #placementConfirmButton {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0;"
        "}"
        "#garmentPlacementPanel #placementConfirmButton:hover {"
        "  background-color: #e8f5e9;"
        "  border: 1px solid #81c784;"
        "  border-radius: 4px;"
        "}"
    );

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    auto* title_label = new QLabel("Garment Placement", this);
    title_label->setStyleSheet("font-size: 16px; font-weight: 600;");
    title_label->setContentsMargins(4, 0, 0, 0);
    auto* cancel_button = new QPushButton(this);
    cancel_button->setObjectName("placementCancelButton");
    cancel_button->setFixedSize(placement_button_size, placement_button_size);
    cancel_button->setIcon(make_close_icon());
    cancel_button->setIconSize(QSize{close_icon_size, close_icon_size});
    cancel_button->setToolTip("Cancel garment placement");

    auto* title_layout = new QHBoxLayout();
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->addWidget(title_label);
    title_layout->addStretch(1);
    title_layout->addWidget(cancel_button);
    root_layout->addLayout(title_layout);

    auto* groups_widget = new QWidget(this);
    auto* groups_layout = new QVBoxLayout(groups_widget);
    groups_layout->setContentsMargins(0, 0, 0, 0);
    groups_layout->setSpacing(8);
    create_group(lower_group_index, groups_widget);
    create_group(upper_group_index, groups_widget);
    add_button_ = new QPushButton(groups_widget);
    add_button_->setObjectName("addUpperButton");
    add_button_->setFixedHeight(add_button_height);
    add_button_->setIcon(make_plus_icon());
    add_button_->setIconSize(QSize{action_icon_size, action_icon_size});
    add_button_->setToolTip("Add upper garment");
    groups_layout->addWidget(groups_[lower_group_index].frame);
    groups_layout->addWidget(groups_[upper_group_index].frame);
    groups_layout->addWidget(add_button_);
    root_layout->addWidget(groups_widget);

    confirm_run_button_ = new QPushButton(this);
    confirm_run_button_->setObjectName("placementConfirmButton");
    confirm_run_button_->setFixedSize(40, 32);
    confirm_run_button_->setIcon(make_confirm_icon());
    confirm_run_button_->setIconSize(QSize{confirm_icon_size, confirm_icon_size});
    confirm_run_button_->setToolTip("Confirm placement & run");
    root_layout->addWidget(confirm_run_button_, 0, Qt::AlignHCenter);

    connect(add_button_, &QPushButton::clicked, this, [this]() {
        if (add_upper_callback_) {
            add_upper_callback_();
        }
    });
    connect(confirm_run_button_, &QPushButton::clicked, this, [this]() {
        if (confirm_run_callback_) {
            confirm_run_callback_();
        }
    });
    connect(cancel_button, &QPushButton::clicked, this, [this]() {
        if (cancel_callback_) {
            cancel_callback_();
        }
    });

    reset_placement();
    setEnabled(false);
}

void GarmentPlacementPanel::create_group(std::size_t group_index, QWidget* parent)
{
    PlacementGroup& group = groups_[group_index];
    group.frame = new QFrame(parent);
    group.frame->setObjectName("placementGroup");

    auto* group_layout = new QVBoxLayout(group.frame);
    group_layout->setContentsMargins(8, 6, 8, 8);
    group_layout->setSpacing(6);

    group.group_label = new QLabel(group_index == lower_group_index ? "Lower" : "Upper", group.frame);
    group.group_label->setStyleSheet("font-size: 16px; font-weight: 600;");
    group.group_label->setContentsMargins(5, 0, 0, 0);
    group.garment_name_label = new QLabel("garment", group.frame);
    group.garment_name_label->setStyleSheet("font-size: 14px; font-weight: 560; color: #3f5f7f;");
    group.garment_name_label->setContentsMargins(5, 0, 0, 10);
    QSizePolicy garment_name_policy = group.garment_name_label->sizePolicy();
    garment_name_policy.setRetainSizeWhenHidden(true);
    group.garment_name_label->setSizePolicy(garment_name_policy);
    group.garment_name_label->setVisible(false);

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(4);
    header_layout->addWidget(group.group_label, 1);
    if (group_index == upper_group_index) {
        auto* remove_button = new QPushButton(group.frame);
        remove_button->setObjectName("upperRemoveButton");
        remove_button->setFixedSize(placement_button_size, placement_button_size);
        remove_button->setIcon(make_close_icon());
        remove_button->setIconSize(QSize{close_icon_size, close_icon_size});
        remove_button->setToolTip("Remove upper garment");
        header_layout->addWidget(remove_button);
        connect(remove_button, &QPushButton::clicked, this, [this]() {
            if (remove_upper_callback_) {
                remove_upper_callback_();
            }
        });
    }
    group_layout->addLayout(header_layout);
    group_layout->addWidget(group.garment_name_label);

    group.controls = new QWidget(group.frame);
    auto* controls_grid = new QGridLayout(group.controls);
    controls_grid->setContentsMargins(0, 0, 0, 10);
    controls_grid->setHorizontalSpacing(6);
    controls_grid->setVerticalSpacing(6);
    controls_grid->setColumnMinimumWidth(0, 36);
    controls_grid->setColumnMinimumWidth(1, 40);
    controls_grid->setColumnStretch(2, 1);

    constexpr std::array<const char*, 3> axis_names{"X", "Y", "Z"};
    for (std::size_t axis = 0; axis < axis_names.size(); ++axis) {
        const int axis_index = static_cast<int>(axis);
        auto* axis_label = new QLabel(axis_names[axis], group.controls);
        axis_label->setAlignment(Qt::AlignCenter);
        group.position_value_labels[axis] = new QLabel(format_float(0.0f), group.controls);
        group.position_value_labels[axis]->setAlignment(Qt::AlignCenter);
        group.position_sliders[axis] = new QSlider(Qt::Horizontal, group.controls);
        group.position_sliders[axis]->setRange(position_slider_min, position_slider_max);
        group.position_sliders[axis]->setValue(position_slider_center);
        group.position_sliders[axis]->setSingleStep(1);
        group.position_sliders[axis]->setPageStep(1);

        controls_grid->addWidget(axis_label, axis_index, 0);
        controls_grid->addWidget(group.position_value_labels[axis], axis_index, 1);
        controls_grid->addWidget(group.position_sliders[axis], axis_index, 2);
        connect(group.position_sliders[axis], &QSlider::valueChanged, this,
            [this, group_index, axis_index](int value) {
                set_position_from_slider(group_index, axis_index, value);
            });
    }

    const int scale_row = static_cast<int>(axis_names.size());
    auto* scale_label = new QLabel("Scale", group.controls);
    scale_label->setAlignment(Qt::AlignCenter);
    controls_grid->addWidget(scale_label, scale_row, 0);
    group.scale_value_label = new QLabel(format_float(group.scale), group.controls);
    group.scale_value_label->setAlignment(Qt::AlignCenter);
    group.scale_slider = new QSlider(Qt::Horizontal, group.controls);
    group.scale_slider->setRange(scale_slider_min, scale_slider_max);
    group.scale_slider->setValue(scale_slider_center);
    group.scale_slider->setSingleStep(1);
    group.scale_slider->setPageStep(1);
    controls_grid->addWidget(group.scale_value_label, scale_row, 1);
    controls_grid->addWidget(group.scale_slider, scale_row, 2);
    connect(group.scale_slider, &QSlider::valueChanged, this, [this, group_index](int value) {
        set_scale_from_slider(group_index, value);
    });
    group_layout->addWidget(group.controls);

    group.color_button = new QPushButton(group.frame);
    group.color_button->setFixedSize(26, 26);
    group.color_button->setToolTip("Choose garment color");
    connect(group.color_button, &QPushButton::clicked, this, [this, group_index]() {
        choose_color(group_index);
    });
    group_layout->addWidget(group.color_button, 0, Qt::AlignHCenter);

    group.frame->installEventFilter(this);
    for (QWidget* child : group.frame->findChildren<QWidget*>()) {
        child->installEventFilter(this);
    }
}

void GarmentPlacementPanel::notify_placement_changed(std::size_t group_index)
{
    if (placement_changed_callback_) {
        const PlacementGroup& group = groups_[group_index];
        placement_changed_callback_(group_index, group.position_offset, group.scale);
    }
}

void GarmentPlacementPanel::update_value_labels(std::size_t group_index)
{
    const PlacementGroup& group = groups_[group_index];
    for (std::size_t index = 0; index < group.position_value_labels.size(); ++index) {
        group.position_value_labels[index]->setText(format_float(group.position_offset[static_cast<int>(index)]));
    }

    group.scale_value_label->setText(format_float(group.scale));
}

void GarmentPlacementPanel::set_position_from_slider(std::size_t group_index, int axis_index, int slider_value)
{
    set_active_group(group_index);
    groups_[group_index].position_offset[axis_index] = static_cast<float>(slider_value) * position_slider_factor;
    update_value_labels(group_index);
    notify_placement_changed(group_index);
}

void GarmentPlacementPanel::set_scale_from_slider(std::size_t group_index, int slider_value)
{
    set_active_group(group_index);
    groups_[group_index].scale = static_cast<float>(slider_value) * scale_slider_factor;
    update_value_labels(group_index);
    notify_placement_changed(group_index);
}

void GarmentPlacementPanel::choose_color(std::size_t group_index)
{
    set_active_group(group_index);
    PlacementGroup& group = groups_[group_index];
    const QColor original_color = QColor::fromRgbF(group.color.r, group.color.g, group.color.b);
    QDialog color_dialog(this);
    color_dialog.setWindowTitle("Garment Color");

    auto* root_layout = new QVBoxLayout(&color_dialog);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(8);

    auto* color_display = new QLabel(&color_dialog);
    color_display->setAlignment(Qt::AlignCenter);
    color_display->setFixedHeight(24);
    root_layout->addWidget(color_display);

    auto* picker_layout = new QHBoxLayout();
    picker_layout->setContentsMargins(0, 0, 0, 0);
    picker_layout->setSpacing(8);

    auto* color_slider = new color_widgets::Color2DSlider(&color_dialog);
    color_slider->setFixedSize(320, 180);
    auto* hue_slider = new QSlider(Qt::Vertical, &color_dialog);
    hue_slider->setRange(0, hue_slider_max);
    hue_slider->setFixedSize(24, 180);
    hue_slider->setStyleSheet(
        "QSlider::groove:vertical {"
        "  background: qlineargradient(x1:0, y1:1, x2:0, y2:0,"
        "    stop:0 #ff0000, stop:0.166 #ffff00, stop:0.333 #00ff00,"
        "    stop:0.5 #00ffff, stop:0.666 #0000ff, stop:0.833 #ff00ff, stop:1 #ff0000);"
        "  width: 16px; border: 1px solid #666666;"
        "}"
        "QSlider::handle:vertical {"
        "  background: transparent; border: 2px solid white; height: 6px; margin: 0 -4px;"
        "}"
    );
    auto* hue_slider_style = new AbsoluteSliderStyle();
    hue_slider_style->setParent(hue_slider);
    hue_slider->setStyle(hue_slider_style);
    picker_layout->addWidget(color_slider);
    picker_layout->addWidget(hue_slider);
    root_layout->addLayout(picker_layout);

    auto* button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok,
        &color_dialog
    );
    root_layout->addWidget(button_box);

    const qreal original_hue = original_color.hsvHueF() < 0.0 ? 0.0 : original_color.hsvHueF();
    color_slider->setColor(QColor::fromHsvF(
        original_hue,
        original_color.saturationF(),
        original_color.valueF()
    ));
    hue_slider->setValue(static_cast<int>(original_hue * hue_slider_max));

    const auto update_display = [color_display](const QColor& color) {
        const QString text_color = color.lightnessF() > 0.5 ? "#111111" : "#ffffff";
        color_display->setText(color.name(QColor::HexRgb).toUpper());
        color_display->setStyleSheet(QString(
            "background-color: %1; color: %2; border: 1px solid #666666;"
        ).arg(color.name(QColor::HexRgb), text_color));
    };
    const auto apply_color = [this, group_index, update_display](const QColor& color) {
        update_display(color);
        PlacementGroup& active_group = groups_[group_index];
        active_group.color = {
            static_cast<float>(color.redF()),
            static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF()),
        };
        update_color_button(group_index);
        if (color_changed_callback_) {
            color_changed_callback_(group_index, active_group.color);
        }
    };

    update_display(original_color);
    connect(
        hue_slider,
        &QSlider::valueChanged,
        &color_dialog,
        [color_slider](int value) {
            color_slider->setHue(static_cast<qreal>(value) / hue_slider_max);
        }
    );
    connect(
        color_slider,
        &color_widgets::Color2DSlider::colorChanged,
        &color_dialog,
        apply_color
    );
    connect(button_box, &QDialogButtonBox::accepted, &color_dialog, &QDialog::accept);

    color_dialog.adjustSize();
    if (color_dialog.exec() != QDialog::Accepted) {
        apply_color(original_color);
    }
}

void GarmentPlacementPanel::update_color_button(std::size_t group_index)
{
    PlacementGroup& group = groups_[group_index];
    const QColor button_color = QColor::fromRgbF(group.color.r, group.color.g, group.color.b);
    group.color_button->setStyleSheet(QString(
        "QPushButton { background-color: rgb(%1, %2, %3); border: 2px solid #666666; border-radius: 13px; padding: 0; }"
        "QPushButton:hover { border-color: #1f6feb; }"
    ).arg(button_color.red()).arg(button_color.green()).arg(button_color.blue()));
}

void GarmentPlacementPanel::reset_group(std::size_t group_index, const glm::vec3& color)
{
    PlacementGroup& group = groups_[group_index];
    group.position_offset = glm::vec3{0.0f};
    group.color = color;
    group.scale = 1.0f;
    group.group_label->setText(group_index == lower_group_index ? "Lower" : "Upper");
    group.garment_name_label->setVisible(false);
    group.controls->setEnabled(false);
    group.color_button->setEnabled(false);

    const QSignalBlocker scale_blocker(*group.scale_slider);
    group.scale_slider->setValue(scale_slider_center);

    for (QSlider* position_slider : group.position_sliders) {
        const QSignalBlocker position_blocker(*position_slider);
        position_slider->setValue(position_slider_center);
    }

    update_value_labels(group_index);
    update_color_button(group_index);
}

void GarmentPlacementPanel::set_active_group(std::size_t group_index)
{
    if (group_index >= groups_.size() || groups_[group_index].frame->isHidden()) {
        return;
    }

    active_group_index_ = group_index;
    for (std::size_t index = 0; index < groups_.size(); ++index) {
        QFrame* frame = groups_[index].frame;
        frame->setProperty("active", index == active_group_index_);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        frame->update();
    }
}

bool GarmentPlacementPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        for (std::size_t index = 0; index < groups_.size(); ++index) {
            QObject* current = watched;
            while (current != nullptr && current != this) {
                if (current == groups_[index].frame) {
                    set_active_group(index);
                    break;
                }
                current = current->parent();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GarmentPlacementPanel::begin_session(std::size_t group_index,
                                          const QString& garment_name,
                                          const glm::vec3& color)
{
    reset_placement();
    if (group_index >= groups_.size()) {
        return;
    }

    groups_[group_index].frame->setVisible(true);
    set_group_garment(group_index, garment_name, color);
    set_active_group(group_index);
}

void GarmentPlacementPanel::set_group_garment(std::size_t group_index,
                                              const QString& garment_name,
                                              const glm::vec3& color)
{
    if (group_index >= groups_.size()) {
        return;
    }

    reset_group(group_index, color);
    PlacementGroup& group = groups_[group_index];
    group.garment_name_label->setText(garment_name);
    group.garment_name_label->setVisible(true);
    group.controls->setEnabled(true);
    group.color_button->setEnabled(true);
}

void GarmentPlacementPanel::show_upper_placeholder()
{
    reset_group(upper_group_index);
    groups_[upper_group_index].frame->setVisible(true);
    set_active_group(upper_group_index);
    set_add_enabled(false);
    set_confirm_enabled(false);
}

void GarmentPlacementPanel::remove_upper_group()
{
    reset_group(upper_group_index);
    groups_[upper_group_index].frame->setVisible(false);
    if (!groups_[lower_group_index].frame->isHidden()) {
        set_active_group(lower_group_index);
    }
}

void GarmentPlacementPanel::set_add_enabled(bool enabled)
{
    add_button_->setEnabled(enabled);
    add_button_->setVisible(enabled);
    QWidget* groups_widget = add_button_->parentWidget();
    groups_widget->layout()->invalidate();
    groups_widget->updateGeometry();
}

void GarmentPlacementPanel::set_confirm_enabled(bool enabled)
{
    confirm_run_button_->setEnabled(enabled);
}

std::size_t GarmentPlacementPanel::active_group_index() const
{
    return active_group_index_;
}

void GarmentPlacementPanel::reset_placement()
{
    for (std::size_t index = 0; index < groups_.size(); ++index) {
        reset_group(index);
        groups_[index].frame->setVisible(false);
    }
    active_group_index_ = lower_group_index;
    set_add_enabled(false);
    set_confirm_enabled(false);
}

// setter //
void GarmentPlacementPanel::set_placement_changed_callback(PlacementChangedCallback callback)
{
    placement_changed_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_color_changed_callback(ColorChangedCallback callback)
{
    color_changed_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_add_upper_callback(AddUpperCallback callback)
{
    add_upper_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_remove_upper_callback(RemoveUpperCallback callback)
{
    remove_upper_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_confirm_run_callback(ConfirmRunCallback callback)
{
    confirm_run_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_cancel_callback(CancelCallback callback)
{
    cancel_callback_ = std::move(callback);
}
