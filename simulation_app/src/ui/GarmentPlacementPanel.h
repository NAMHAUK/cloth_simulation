#pragma once

#include <array>
#include <functional>

#include <glm/vec3.hpp>

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

class GarmentPlacementPanel final : public QWidget {
public:
    using PlacementChangedCallback = std::function<void(const glm::vec3& position_offset, float scale)>;
    using ConfirmRunCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;

    explicit GarmentPlacementPanel(QWidget* parent = nullptr);

    void set_placement_changed_callback(PlacementChangedCallback callback);
    void set_confirm_run_callback(ConfirmRunCallback callback);
    void set_cancel_callback(CancelCallback callback);
    void reset_placement();

private:
    void set_position_from_slider(int axis_index, int slider_value);
    void set_scale_from_slider(int slider_value);
    void notify_placement_changed();
    void update_value_labels();

    glm::vec3 position_offset_{0.0f};
    float scale_ = 1.0f;
    std::array<QLabel*, 3> position_value_labels_{};
    std::array<QSlider*, 3> position_sliders_{};
    QLabel* scale_value_label_ = nullptr;
    QSlider* scale_slider_ = nullptr;
    QPushButton* confirm_run_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    PlacementChangedCallback placement_changed_callback_;
    ConfirmRunCallback confirm_run_callback_;
    CancelCallback cancel_callback_;
};
