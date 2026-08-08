#pragma once

#include <glm/vec3.hpp>

#include <QColor>
#include <QFrame>

class QEvent;
class QLineEdit;
class QSlider;
class QVBoxLayout;

class GarmentColorPanel final : public QFrame
{
    Q_OBJECT

public:
    explicit GarmentColorPanel(QWidget* parent = nullptr);

    void open_panel(const glm::vec3& color);
    void close_panel();

Q_SIGNALS:
    void color_changed(const glm::vec3& color);
    void visibility_changed(bool visible);

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
};
