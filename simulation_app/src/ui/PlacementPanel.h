#pragma once

#include "asset/AssetDataTypes.h"

#include <array>
#include <functional>

#include <glm/vec3.hpp>

#include <QWidget>

class QEvent;
class QFrame;
class QLabel;
class QPushButton;
class QSlider;
class QString;
class QVBoxLayout;

class PlacementPanel final : public QWidget
{
public:
    using PlacementChangedCallback =
        std::function<void(GarmentLayer layer, const glm::vec3& position_offset, float scale)>;
    using ColorChangedCallback = std::function<void(GarmentLayer layer, const glm::vec3& color)>;
    using ColorSelectedCallback = std::function<void(const glm::vec3& color)>;
    using OpenColorEditorCallback = std::function<
        void(GarmentLayer layer, const glm::vec3& color, ColorSelectedCallback color_selected_callback)>;
    using AddUpperPlacementCallback = std::function<void()>;
    using RemoveUpperPlacementCallback = std::function<void()>;
    using ConfirmCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;

    explicit PlacementPanel(QWidget* parent = nullptr);

    void set_garment(GarmentLayer layer, const QString& garment_name, const glm::vec3& color);
    void show_upper_section();
    void hide_upper_section();
    void set_add_button_enabled(bool enabled);
    void set_confirm_button_enabled(bool enabled);
    void reset();

    GarmentLayer active_layer() const;

    void set_placement_changed_callback(PlacementChangedCallback callback);
    void set_color_changed_callback(ColorChangedCallback callback);
    void set_open_color_editor_callback(OpenColorEditorCallback callback);
    void set_add_upper_placement_callback(AddUpperPlacementCallback callback);
    void set_remove_upper_placement_callback(RemoveUpperPlacementCallback callback);
    void set_confirm_callback(ConfirmCallback callback);
    void set_cancel_callback(CancelCallback callback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PlacementState final
    {
        QFrame* frame = nullptr;
        QLabel* garment_name_label = nullptr;
        QWidget* transform_controls = nullptr;
        glm::vec3 position_offset{0.0f};
        glm::vec3 color{1.0f};
        float scale = 1.0f;
        std::array<QLabel*, 3> position_value_labels{};
        std::array<QSlider*, 3> position_sliders{};
        QLabel* scale_value_label = nullptr;
        QSlider* scale_slider = nullptr;
        QPushButton* color_button = nullptr;
    };

    void setup_style();
    void setup_header(QVBoxLayout& root_layout);
    void setup_garment_sections(QVBoxLayout& root_layout);
    void setup_confirm_button(QVBoxLayout& root_layout);
    void setup_garment_section(GarmentLayer layer);
    void setup_section_header(GarmentLayer layer, QVBoxLayout& section_layout);
    void setup_transform_controls(GarmentLayer layer, QVBoxLayout& section_layout);

    void reset_section_values(GarmentLayer layer, const glm::vec3& color = glm::vec3{1.0f});

    void update_garment_position(GarmentLayer layer, int axis_index, int slider_value);
    void update_garment_scale(GarmentLayer layer, int slider_value);
    void choose_garment_color(GarmentLayer layer);
    void set_active_layer(GarmentLayer selected_layer);

    std::array<PlacementState, 2> placement_states_{};
    GarmentLayer active_layer_ = GarmentLayer::Lower;
    QPushButton* add_button_ = nullptr;
    QPushButton* upper_remove_button_ = nullptr;
    QPushButton* confirm_run_button_ = nullptr;
    PlacementChangedCallback placement_changed_callback_;
    ColorChangedCallback color_changed_callback_;
    OpenColorEditorCallback open_color_editor_callback_;
    AddUpperPlacementCallback add_upper_placement_callback_;
    RemoveUpperPlacementCallback remove_upper_placement_callback_;
    ConfirmCallback confirm_callback_;
    CancelCallback cancel_callback_;
};
