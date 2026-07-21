#include "ui/GarmentPlacementPanel.h"

#include <array>
#include <cstddef>
#include <utility>

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProxyStyle>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
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
    );

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    auto* title_label = new QLabel("Garment Placement", this);
    title_label->setStyleSheet("font-weight: 600;");
    root_layout->addWidget(title_label);

    auto* controls_container = new QWidget(this);
    auto* controls_grid = new QGridLayout(controls_container);
    controls_grid->setContentsMargins(0, 0, 0, 0);
    controls_grid->setHorizontalSpacing(6);
    controls_grid->setVerticalSpacing(6);
    controls_grid->setColumnMinimumWidth(0, 42);
    controls_grid->setColumnMinimumWidth(1, 48);
    controls_grid->setColumnStretch(2, 1);

    constexpr std::array<const char*, 3> axis_names{"X", "Y", "Z"};
    for (std::size_t index = 0; index < axis_names.size(); ++index) {
        const int axis_index = static_cast<int>(index);
        auto* axis_label = new QLabel(axis_names[index], controls_container);

        auto* value_label = new QLabel(format_float(0.0f), controls_container);
        position_value_labels_[index] = value_label;

        auto* position_slider = new QSlider(Qt::Horizontal, controls_container);
        position_slider->setRange(position_slider_min, position_slider_max);
        position_slider->setValue(position_slider_center);
        position_slider->setSingleStep(1);
        position_slider->setPageStep(1);
        position_sliders_[index] = position_slider;

        controls_grid->addWidget(axis_label, axis_index, 0);
        controls_grid->addWidget(value_label, axis_index, 1);
        controls_grid->addWidget(position_slider, axis_index, 2);

        connect(position_slider, &QSlider::valueChanged, this, [this, axis_index](int value) {
            set_position_from_slider(axis_index, value);
        });
    }

    const int scale_row = static_cast<int>(axis_names.size());
    auto* scale_label = new QLabel("Scale", controls_container);

    scale_value_label_ = new QLabel(format_float(scale_), controls_container);

    scale_slider_ = new QSlider(Qt::Horizontal, controls_container);
    scale_slider_->setRange(scale_slider_min, scale_slider_max);
    scale_slider_->setValue(scale_slider_center);
    scale_slider_->setSingleStep(1);
    scale_slider_->setPageStep(1);

    controls_grid->addWidget(scale_label, scale_row, 0);
    controls_grid->addWidget(scale_value_label_, scale_row, 1);
    controls_grid->addWidget(scale_slider_, scale_row, 2);

    root_layout->addWidget(controls_container);

    color_button_ = new QPushButton(this);
    color_button_->setFixedSize(26, 26);
    color_button_->setToolTip("Choose garment color");
    update_color_button();

    root_layout->addWidget(color_button_, 0, Qt::AlignHCenter);
    root_layout->addStretch(1);

    confirm_run_button_ = new QPushButton("Confirm&Run", this);
    confirm_run_button_->setMinimumWidth(120);
    cancel_button_ = new QPushButton("Cancel", this);
    cancel_button_->setMinimumWidth(80);

    auto* button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);
    button_layout->addStretch(1);
    button_layout->addWidget(confirm_run_button_);
    button_layout->addWidget(cancel_button_);
    button_layout->addStretch(1);
    root_layout->addLayout(button_layout);

    connect(scale_slider_, &QSlider::valueChanged, this, [this](int value) {
        set_scale_from_slider(value);
    });
    connect(color_button_, &QPushButton::clicked, this, &GarmentPlacementPanel::choose_color);
    connect(confirm_run_button_, &QPushButton::clicked, this, [this]() {
        if (confirm_run_callback_) {
            confirm_run_callback_();
        }
    });
    connect(cancel_button_, &QPushButton::clicked, this, [this]() {
        if (cancel_callback_) {
            cancel_callback_();
        }
    });

    setEnabled(false);
}

void GarmentPlacementPanel::notify_placement_changed()
{
    if (placement_changed_callback_) {
        placement_changed_callback_(position_offset_, scale_);
    }
}

void GarmentPlacementPanel::update_value_labels()
{
    for (std::size_t index = 0; index < position_value_labels_.size(); ++index) {
        position_value_labels_[index]->setText(format_float(position_offset_[static_cast<int>(index)]));
    }

    scale_value_label_->setText(format_float(scale_));
}

void GarmentPlacementPanel::set_position_from_slider(int axis_index, int slider_value)
{
    position_offset_[axis_index] = static_cast<float>(slider_value) * position_slider_factor;
    update_value_labels();
    notify_placement_changed();
}

void GarmentPlacementPanel::set_scale_from_slider(int slider_value)
{
    scale_ = static_cast<float>(slider_value) * scale_slider_factor;
    update_value_labels();
    notify_placement_changed();
}

void GarmentPlacementPanel::choose_color()
{
    const QColor original_color = QColor::fromRgbF(color_.r, color_.g, color_.b);
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
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
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
    const auto apply_color = [this, update_display](const QColor& color) {
        update_display(color);
        set_color({
            static_cast<float>(color.redF()),
            static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF()),
        });
        if (color_changed_callback_) {
            color_changed_callback_(color_);
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
    connect(button_box, &QDialogButtonBox::rejected, &color_dialog, &QDialog::reject);

    if (color_dialog.exec() != QDialog::Accepted) {
        apply_color(original_color);
    }
}

void GarmentPlacementPanel::update_color_button()
{
    const QColor button_color = QColor::fromRgbF(color_.r, color_.g, color_.b);
    color_button_->setStyleSheet(QString(
        "QPushButton { background-color: rgb(%1, %2, %3); border: 2px solid #666666; border-radius: 13px; padding: 0; }"
        "QPushButton:hover { border-color: #1f6feb; }"
    ).arg(button_color.red()).arg(button_color.green()).arg(button_color.blue()));
}

void GarmentPlacementPanel::reset_placement()
{
    position_offset_ = glm::vec3{0.0f};
    scale_ = 1.0f;

    const QSignalBlocker scale_blocker(*scale_slider_);
    scale_slider_->setValue(scale_slider_center);

    for (QSlider* position_slider : position_sliders_) {
        const QSignalBlocker position_blocker(*position_slider);
        position_slider->setValue(position_slider_center);
    }

    update_value_labels();
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

void GarmentPlacementPanel::set_confirm_run_callback(ConfirmRunCallback callback)
{
    confirm_run_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_cancel_callback(CancelCallback callback)
{
    cancel_callback_ = std::move(callback);
}

void GarmentPlacementPanel::set_color(const glm::vec3& color)
{
    color_ = color;
    update_color_button();
}
