#include "ui/PlacementPanel.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int position_slider_min = -30;
constexpr int position_slider_max = 30;
constexpr int scale_slider_min = 50;
constexpr int scale_slider_max = 150;
constexpr int scale_slider_center = 100;
constexpr float slider_factor = 0.01f;

constexpr int placement_button_size = 26;
constexpr int close_icon_size = 12;
constexpr int action_icon_size = 20;
constexpr int add_button_height = 30;
constexpr int confirm_icon_size = 28;

const QString color_button_style = QStringLiteral(
    "QPushButton { background-color: %1; border: 2px solid #666666; border-radius: 13px; padding: 0; }"
    "QPushButton:hover { border-color: #1f6feb; }");

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

PlacementPanel::PlacementPanel(QWidget* parent) : QWidget(parent)
{
    setup_style();

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    setup_header(*root_layout);
    setup_garment_sections(*root_layout);
    setup_confirm_button(*root_layout);

    reset();
    setEnabled(false);
}

// Initialization
void PlacementPanel::setup_style()
{
    setObjectName("garmentPlacementPanel");
    setStyleSheet(R"(
        #garmentPlacementPanel {
            background-color: rgba(245, 245, 245, 235);
            border: 1px solid #9a9a9a;
            border-radius: 4px;
        }
        QLabel {
            border: none;
            background: transparent;
        }
        QPushButton {
            background-color: #1f6feb;
            color: white;
            border: 1px solid #1158c7;
            border-radius: 4px;
            padding: 2px 8px;
        }
        QPushButton:hover {
            background-color: #2f81f7;
        }
        QPushButton:disabled {
            background-color: #8caee6;
            color: #dddddd;
        }
        QSlider {
            background: transparent;
            border: none;
        }
        #placementGroup {
            background-color: rgba(255, 255, 255, 150);
            border: 1px solid #b8b8b8;
            border-radius: 4px;
        }
        #placementGroup[active="true"] {
            border: 2px solid #1f6feb;
        }
        #placementCancelButton {
            padding: 0;
            background-color: #5a5a5a;
            border: 1px solid #242424;
        }
        #placementCancelButton:hover {
            background-color: #7a7a7a;
        }
        #upperRemoveButton, #addUpperButton {
            padding: 0;
        }
        #placementConfirmButton {
            background: transparent;
            border: none;
            padding: 0;
        }
        #placementConfirmButton:hover {
            background-color: #e8f5e9;
            border: 1px solid #81c784;
            border-radius: 4px;
        }
    )");
}

void PlacementPanel::setup_header(QVBoxLayout& root_layout)
{
    auto* title_label = new QLabel("Garment Placement", this);
    title_label->setStyleSheet("font-size: 16px; font-weight: 600;");
    title_label->setContentsMargins(4, 0, 0, 0);

    auto* cancel_button = new QPushButton(this);
    cancel_button->setObjectName("placementCancelButton");
    cancel_button->setFixedSize(placement_button_size, placement_button_size);
    cancel_button->setIcon(make_close_icon());
    cancel_button->setIconSize(QSize{close_icon_size, close_icon_size});
    cancel_button->setToolTip("Cancel garment placement");
    connect(cancel_button, &QPushButton::clicked, this, [this]() { cancel_callback_(); });

    auto* title_layout = new QHBoxLayout();
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->addWidget(title_label);
    title_layout->addStretch(1);
    title_layout->addWidget(cancel_button);

    root_layout.addLayout(title_layout);
}

void PlacementPanel::setup_garment_sections(QVBoxLayout& root_layout)
{
    setup_garment_section(GarmentLayer::Lower);
    setup_garment_section(GarmentLayer::Upper);

    add_button_ = new QPushButton(this);
    add_button_->setObjectName("addUpperButton");
    add_button_->setFixedHeight(add_button_height);
    add_button_->setIcon(make_plus_icon());
    add_button_->setIconSize(QSize{action_icon_size, action_icon_size});
    add_button_->setToolTip("Add upper garment");
    connect(add_button_, &QPushButton::clicked, this, [this]() { add_upper_placement_callback_(); });

    root_layout.addWidget(placement_states_[GarmentLayer::Lower].frame);
    root_layout.addWidget(placement_states_[GarmentLayer::Upper].frame);
    root_layout.addWidget(add_button_);
}

void PlacementPanel::setup_confirm_button(QVBoxLayout& root_layout)
{
    confirm_run_button_ = new QPushButton(this);
    confirm_run_button_->setObjectName("placementConfirmButton");
    confirm_run_button_->setFixedSize(40, 32);
    confirm_run_button_->setIcon(make_confirm_icon());
    confirm_run_button_->setIconSize(QSize{confirm_icon_size, confirm_icon_size});
    confirm_run_button_->setToolTip("Confirm placement & run");
    connect(confirm_run_button_, &QPushButton::clicked, this, [this]() { confirm_callback_(); });

    root_layout.addWidget(confirm_run_button_, 0, Qt::AlignHCenter);
}

void PlacementPanel::setup_garment_section(GarmentLayer layer)
{
    PlacementState& state = placement_states_[layer];
    state.frame = new QFrame(this);
    state.frame->setObjectName("placementGroup");

    auto* section_layout = new QVBoxLayout(state.frame);
    section_layout->setContentsMargins(8, 6, 8, 8);
    section_layout->setSpacing(6);

    setup_section_header(layer, *section_layout);
    setup_transform_controls(layer, *section_layout);

    // color button
    state.color_button = new QPushButton(state.frame);
    state.color_button->setFixedSize(26, 26);
    state.color_button->setToolTip("Choose garment color");
    connect(state.color_button, &QPushButton::clicked, this, [this, layer]() {
        choose_garment_color(layer);
    });
    section_layout->addWidget(state.color_button, 0, Qt::AlignHCenter);

    // Register section click event
    state.frame->installEventFilter(this);
    for (QWidget* child : state.frame->findChildren<QWidget*>()) {
        child->installEventFilter(this);
    }
}

void PlacementPanel::setup_section_header(GarmentLayer layer, QVBoxLayout& section_layout)
{
    PlacementState& state = placement_states_[layer];

    // Header layout
    auto* layer_label = new QLabel(layer == GarmentLayer::Lower ? "Lower" : "Upper", state.frame);
    layer_label->setStyleSheet("font-size: 16px; font-weight: 600;");
    layer_label->setContentsMargins(5, 0, 0, 0);

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(4);
    header_layout->addWidget(layer_label, 1);

    if (layer == GarmentLayer::Upper) {
        upper_remove_button_ = new QPushButton(state.frame);
        upper_remove_button_->setObjectName("upperRemoveButton");
        upper_remove_button_->setFixedSize(placement_button_size, placement_button_size);
        upper_remove_button_->setIcon(make_close_icon());
        upper_remove_button_->setIconSize(QSize{close_icon_size, close_icon_size});
        upper_remove_button_->setToolTip("Remove upper garment");
        header_layout->addWidget(upper_remove_button_);
        connect(upper_remove_button_, &QPushButton::clicked, this, [this]() {
            remove_upper_placement_callback_();
        });
    }

    section_layout.addLayout(header_layout);

    // Garment name
    state.garment_name_label = new QLabel(state.frame);
    state.garment_name_label->setStyleSheet("font-size: 14px; font-weight: 560; color: #3f5f7f;");
    state.garment_name_label->setContentsMargins(5, 0, 0, 10);

    QSizePolicy garment_name_policy = state.garment_name_label->sizePolicy();
    garment_name_policy.setRetainSizeWhenHidden(true);
    state.garment_name_label->setSizePolicy(garment_name_policy);

    section_layout.addWidget(state.garment_name_label);
}

void PlacementPanel::setup_transform_controls(GarmentLayer layer, QVBoxLayout& section_layout)
{
    PlacementState& state = placement_states_[layer];

    state.transform_controls = new QWidget(state.frame);
    auto* transform_layout = new QGridLayout(state.transform_controls);
    transform_layout->setContentsMargins(0, 0, 0, 10);
    transform_layout->setSpacing(6);
    transform_layout->setColumnMinimumWidth(0, 36);
    transform_layout->setColumnMinimumWidth(1, 40);
    transform_layout->setColumnStretch(2, 1);

    // transform control label & slider
    // ex) X   [0.00]   slider
    constexpr std::array<const char*, 3> axis_names{"X", "Y", "Z"};
    for (int axis = 0; axis < static_cast<int>(axis_names.size()); ++axis) {
        auto* axis_label = new QLabel(axis_names[axis], state.transform_controls);
        axis_label->setAlignment(Qt::AlignCenter);
        transform_layout->addWidget(axis_label, axis, 0);

        state.position_value_labels[axis] = new QLabel(state.transform_controls);
        state.position_value_labels[axis]->setAlignment(Qt::AlignCenter);
        transform_layout->addWidget(state.position_value_labels[axis], axis, 1);

        state.position_sliders[axis] = new QSlider(Qt::Horizontal, state.transform_controls);
        state.position_sliders[axis]->setRange(position_slider_min, position_slider_max);
        state.position_sliders[axis]->setPageStep(1);
        transform_layout->addWidget(state.position_sliders[axis], axis, 2);

        connect(state.position_sliders[axis], &QSlider::valueChanged, this, [this, layer, axis](int value) {
            update_garment_position(layer, axis, value);
        });
    }

    // scale control label & slider
    // scale   [1.00]   slider
    const int scale_row = static_cast<int>(axis_names.size());
    auto* scale_label = new QLabel("Scale", state.transform_controls);
    scale_label->setAlignment(Qt::AlignCenter);
    transform_layout->addWidget(scale_label, scale_row, 0);

    state.scale_value_label = new QLabel(state.transform_controls);
    state.scale_value_label->setAlignment(Qt::AlignCenter);
    transform_layout->addWidget(state.scale_value_label, scale_row, 1);

    state.scale_slider = new QSlider(Qt::Horizontal, state.transform_controls);
    state.scale_slider->setRange(scale_slider_min, scale_slider_max);
    state.scale_slider->setPageStep(1);
    transform_layout->addWidget(state.scale_slider, scale_row, 2);

    connect(state.scale_slider, &QSlider::valueChanged, this, [this, layer](int value) {
        update_garment_scale(layer, value);
    });

    section_layout.addWidget(state.transform_controls);
}

// UI Updates
void PlacementPanel::set_garment(GarmentLayer layer, const QString& garment_name)
{
    reset_section_values(layer);

    PlacementState& state = placement_states_[layer];
    state.frame->setVisible(true);
    state.garment_name_label->setText(garment_name);
    state.garment_name_label->setVisible(true);
    state.transform_controls->setEnabled(true);
    state.color_button->setEnabled(true);

    set_active_layer(layer);
    if (layer == GarmentLayer::Upper) {
        set_add_button_enabled(false);
        set_confirm_button_enabled(true);
    } else if (placement_states_[GarmentLayer::Upper].frame->isHidden()) {
        set_add_button_enabled(true);
        set_confirm_button_enabled(true);
    }
}

void PlacementPanel::show_upper_section()
{
    reset_section_values(GarmentLayer::Upper);

    PlacementState& state = placement_states_[GarmentLayer::Upper];
    state.garment_name_label->setVisible(false);
    state.transform_controls->setEnabled(false);
    state.color_button->setEnabled(false);
    state.frame->setVisible(true);
    upper_remove_button_->setVisible(true);

    set_active_layer(GarmentLayer::Upper);
    set_add_button_enabled(false);
    set_confirm_button_enabled(false);
}

void PlacementPanel::hide_upper_section()
{
    placement_states_[GarmentLayer::Upper].frame->setVisible(false);
    set_active_layer(GarmentLayer::Lower);
    set_add_button_enabled(true);
    set_confirm_button_enabled(true);
}

void PlacementPanel::set_add_button_enabled(bool enabled)
{
    add_button_->setEnabled(enabled);
    add_button_->setVisible(enabled);
    layout()->invalidate();
    updateGeometry();
}

void PlacementPanel::set_confirm_button_enabled(bool enabled)
{
    confirm_run_button_->setEnabled(enabled);
}

void PlacementPanel::reset()
{
    placement_states_[GarmentLayer::Lower].frame->setVisible(false);
    placement_states_[GarmentLayer::Upper].frame->setVisible(false);
    upper_remove_button_->setVisible(false);
    active_layer_ = GarmentLayer::Lower;
    set_add_button_enabled(false);
    set_confirm_button_enabled(false);
}

void PlacementPanel::reset_section_values(GarmentLayer layer)
{
    PlacementState& state = placement_states_[layer];

    // position state reset
    state.position_offset = glm::vec3{0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        state.position_value_labels[axis]->setText(format_float(state.position_offset[axis]));
    }
    for (QSlider* position_slider : state.position_sliders) {
        const QSignalBlocker position_blocker(*position_slider);
        position_slider->setValue(0);
    }

    // scale state reset
    state.scale = 1.0f;
    state.scale_value_label->setText(format_float(state.scale));
    const QSignalBlocker scale_blocker(*state.scale_slider);
    state.scale_slider->setValue(scale_slider_center);

    // color state reset
    state.color = glm::vec3{1.0f};
    const QColor button_color = QColor::fromRgbF(state.color.r, state.color.g, state.color.b);
    state.color_button->setStyleSheet(color_button_style.arg(button_color.name()));
}

// Placement Interaction
bool PlacementPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = static_cast<QWidget*>(watched);

        for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
            QFrame* frame = placement_states_[layer].frame;
            const bool is_in_section = widget == frame || frame->isAncestorOf(widget);
            if (!is_in_section)
                continue;

            set_active_layer(layer);
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlacementPanel::update_garment_position(GarmentLayer layer, int axis_index, int slider_value)
{
    set_active_layer(layer);

    PlacementState& state = placement_states_[layer];
    state.position_offset[axis_index] = slider_value * slider_factor;
    state.position_value_labels[axis_index]->setText(format_float(state.position_offset[axis_index]));

    placement_changed_callback_(layer, state.position_offset, state.scale);
}

void PlacementPanel::update_garment_scale(GarmentLayer layer, int slider_value)
{
    set_active_layer(layer);

    PlacementState& state = placement_states_[layer];
    state.scale = slider_value * slider_factor;
    state.scale_value_label->setText(format_float(state.scale));

    placement_changed_callback_(layer, state.position_offset, state.scale);
}

void PlacementPanel::choose_garment_color(GarmentLayer layer)
{
    set_active_layer(layer);

    PlacementState& state = placement_states_[layer];
    open_color_editor_callback_(layer, state.color, [this, layer](const glm::vec3& color) {
        PlacementState& state = placement_states_[layer];
        state.color = color;
        const QColor button_color = QColor::fromRgbF(color.r, color.g, color.b);
        state.color_button->setStyleSheet(color_button_style.arg(button_color.name()));

        color_changed_callback_(layer, state.color);
    });
}

void PlacementPanel::set_active_layer(GarmentLayer selected_layer)
{
    active_layer_ = selected_layer;
    for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
        QFrame* frame = placement_states_[layer].frame;
        frame->setProperty("active", layer == active_layer_);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        frame->update();
    }
}

// Accessors
GarmentLayer PlacementPanel::active_layer() const
{
    return active_layer_;
}

// Callback Registration
void PlacementPanel::set_placement_changed_callback(PlacementChangedCallback callback)
{
    placement_changed_callback_ = std::move(callback);
}

void PlacementPanel::set_color_changed_callback(ColorChangedCallback callback)
{
    color_changed_callback_ = std::move(callback);
}

void PlacementPanel::set_open_color_editor_callback(OpenColorEditorCallback callback)
{
    open_color_editor_callback_ = std::move(callback);
}

void PlacementPanel::set_add_upper_placement_callback(AddUpperPlacementCallback callback)
{
    add_upper_placement_callback_ = std::move(callback);
}

void PlacementPanel::set_remove_upper_placement_callback(RemoveUpperPlacementCallback callback)
{
    remove_upper_placement_callback_ = std::move(callback);
}

void PlacementPanel::set_confirm_callback(ConfirmCallback callback)
{
    confirm_callback_ = std::move(callback);
}

void PlacementPanel::set_cancel_callback(CancelCallback callback)
{
    cancel_callback_ = std::move(callback);
}
