#pragma once

#include <functional>

#include <glm/vec3.hpp>

#include <QColor>
#include <QWidget>

class QEvent;
class QLineEdit;
class QSlider;
class QVBoxLayout;

class GarmentColorPanel final : public QWidget
{
public:
    using ColorChangedCallback = std::function<void(const glm::vec3& color)>;
    using VisibilityChangedCallback = std::function<void(bool visible)>;

    explicit GarmentColorPanel(QWidget* parent = nullptr);

    void open_panel(const glm::vec3& color, ColorChangedCallback color_changed_callback);
    void close_panel();
    void set_visibility_changed_callback(VisibilityChangedCallback callback);

private:
    class SaturationValuePicker;

    void setup_header(QVBoxLayout& root_layout);
    void setup_picker(QVBoxLayout& root_layout);

    bool eventFilter(QObject* watched, QEvent* event) override;
    void apply_color_hex();
    void apply_color(const QColor& color);
    void set_color(const QColor& color);

    SaturationValuePicker* saturation_value_picker_ = nullptr;
    QSlider* hue_slider_ = nullptr;
    QLineEdit* color_hex_ = nullptr;
    QColor color_;
    ColorChangedCallback color_changed_callback_;
    VisibilityChangedCallback visibility_changed_callback_;
};
