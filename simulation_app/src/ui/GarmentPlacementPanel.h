#pragma once

#include <array>
#include <cstddef>
#include <functional>

#include <glm/vec3.hpp>

#include <QWidget>

class QEvent;
class QFrame;
class QLabel;
class QPushButton;
class QSlider;
class QString;

class GarmentPlacementPanel final : public QWidget {
public:
    static constexpr std::size_t lower_group_index = 0u;
    static constexpr std::size_t upper_group_index = 1u;

    using PlacementChangedCallback = std::function<void(std::size_t group_index,
                                                        const glm::vec3& position_offset,
                                                        float scale)>;
    using ColorChangedCallback = std::function<void(std::size_t group_index, const glm::vec3& color)>;
    using ColorSelectedCallback = std::function<void(const glm::vec3& color)>;
    using AddUpperCallback = std::function<void()>;
    using RemoveUpperCallback = std::function<void()>;
    using ConfirmRunCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;

    explicit GarmentPlacementPanel(QWidget* parent = nullptr);

    void set_placement_changed_callback(PlacementChangedCallback callback);
    void set_color_changed_callback(ColorChangedCallback callback);
    void set_add_upper_callback(AddUpperCallback callback);
    void set_remove_upper_callback(RemoveUpperCallback callback);
    void set_confirm_run_callback(ConfirmRunCallback callback);
    void set_cancel_callback(CancelCallback callback);
    void begin_session(std::size_t group_index, const QString& garment_name, const glm::vec3& color);
    void set_group_garment(std::size_t group_index, const QString& garment_name, const glm::vec3& color);
    void show_upper_placeholder();
    void remove_upper_group();
    void set_add_enabled(bool enabled);
    void set_confirm_enabled(bool enabled);
    std::size_t active_group_index() const;
    void reset_placement();
    void choose_color(const glm::vec3& initial_color, ColorSelectedCallback color_selected_callback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PlacementGroup final {
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

    void create_group(std::size_t group_index, QWidget* parent);
    void set_active_group(std::size_t group_index);
    void set_position_from_slider(std::size_t group_index, int axis_index, int slider_value);
    void set_scale_from_slider(std::size_t group_index, int slider_value);
    void choose_group_color(std::size_t group_index);
    void notify_placement_changed(std::size_t group_index);
    void update_color_button(std::size_t group_index);
    void update_value_labels(std::size_t group_index);
    void reset_group(std::size_t group_index, const glm::vec3& color = glm::vec3{1.0f});

    std::array<PlacementGroup, 2> groups_{};
    std::size_t active_group_index_ = lower_group_index;
    QPushButton* add_button_ = nullptr;
    QPushButton* confirm_run_button_ = nullptr;
    PlacementChangedCallback placement_changed_callback_;
    ColorChangedCallback color_changed_callback_;
    AddUpperCallback add_upper_callback_;
    RemoveUpperCallback remove_upper_callback_;
    ConfirmRunCallback confirm_run_callback_;
    CancelCallback cancel_callback_;
};
