#pragma once

#include <functional>

#include <glm/vec3.hpp>

#include <QColor>
#include <QWidget>

class QEvent;
class QLineEdit;
class QSlider;

class GarmentColorPanel final : public QWidget {
public:
    using ColorChangedCallback = std::function<void(const glm::vec3& color)>;
    using VisibilityChangedCallback = std::function<void()>;

    explicit GarmentColorPanel(QWidget* parent = nullptr);

    void edit_color(const glm::vec3& color, ColorChangedCallback color_changed_callback);
    void close_panel();
    void set_visibility_changed_callback(VisibilityChangedCallback callback);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void apply_color_text();
    void apply_color(const QColor& color);
    void sync_picker(const QColor& color);
    void update_display(const QColor& color);

    QWidget* color_slider_ = nullptr;
    QSlider* hue_slider_ = nullptr;
    QLineEdit* color_display_ = nullptr;
    QColor color_;
    ColorChangedCallback color_changed_callback_;
    VisibilityChangedCallback visibility_changed_callback_;
};
