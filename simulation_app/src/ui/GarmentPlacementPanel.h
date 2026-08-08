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

class GarmentPlacementPanel final : public QWidget
{
public:
    using PlacementChangedCallback =
        std::function<void(GarmentLayer layer, const glm::vec3& position_offset, float scale)>;
    using ColorChangedCallback = std::function<void(GarmentLayer layer, const glm::vec3& color)>;
    using ColorSelectedCallback = std::function<void(const glm::vec3& color)>;
    using ColorEditCallback =
        std::function<void(const glm::vec3& color, ColorSelectedCallback color_selected_callback)>;
    using AddUpperCallback = std::function<void()>;
    using RemoveUpperCallback = std::function<void()>;
    using ConfirmRunCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;

    explicit GarmentPlacementPanel(QWidget* parent = nullptr);

    void set_placement_changed_callback(PlacementChangedCallback callback);
    void set_color_changed_callback(ColorChangedCallback callback);
    void set_color_edit_callback(ColorEditCallback callback);
    void set_add_upper_callback(AddUpperCallback callback);
    void set_remove_upper_callback(RemoveUpperCallback callback);
    void set_confirm_run_callback(ConfirmRunCallback callback);
    void set_cancel_callback(CancelCallback callback);
    void begin_session(GarmentLayer layer, const QString& garment_name, const glm::vec3& color);
    void set_group_garment(GarmentLayer layer, const QString& garment_name, const glm::vec3& color);
    void show_upper_placeholder();
    void remove_upper_group();
    void set_add_enabled(bool enabled);
    void set_confirm_enabled(bool enabled);
    GarmentLayer active_layer() const;
    void reset_placement();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PlacementGroup final
    {
        QFrame* frame = nullptr;
        QLabel* group_label = nullptr;
        QLabel* garment_name_label = nullptr;
        QWidget* controls = nullptr;
        glm::vec3 position_offset{0.0f};
        glm::vec3 color{1.0f};
        float scale = 1.0f;
        std::array<QLabel*, 3> position_value_labels{};
        std::array<QSlider*, 3> position_sliders{};
        QLabel* scale_value_label = nullptr;
        QSlider* scale_slider = nullptr;
        QPushButton* color_button = nullptr;
    };

    void create_group(GarmentLayer layer, QWidget* parent);
    void set_active_group(GarmentLayer layer);
    void set_position_from_slider(GarmentLayer layer, int axis_index, int slider_value);
    void set_scale_from_slider(GarmentLayer layer, int slider_value);
    void choose_group_color(GarmentLayer layer);
    void notify_placement_changed(GarmentLayer layer);
    void update_color_button(GarmentLayer layer);
    void update_value_labels(GarmentLayer layer);
    void reset_group(GarmentLayer layer, const glm::vec3& color = glm::vec3{1.0f});

    std::array<PlacementGroup, 2> groups_{};
    GarmentLayer active_layer_ = GarmentLayer::Lower;
    QPushButton* add_button_ = nullptr;
    QPushButton* confirm_run_button_ = nullptr;
    PlacementChangedCallback placement_changed_callback_;
    ColorChangedCallback color_changed_callback_;
    ColorEditCallback color_edit_callback_;
    AddUpperCallback add_upper_callback_;
    RemoveUpperCallback remove_upper_callback_;
    ConfirmRunCallback confirm_run_callback_;
    CancelCallback cancel_callback_;
};
