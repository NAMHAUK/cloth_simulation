#include "ui/GarmentColorPanel.h"

#include <utility>

#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

namespace {
constexpr int hue_slider_max = 359;
constexpr int color_update_interval_ms = 33;
constexpr int close_button_size = 26;
constexpr int close_icon_size = 12;
constexpr int color_slider_width = 226;
constexpr int color_slider_height = 150;
constexpr int hue_slider_width = 26;
constexpr qreal color_selector_radius = 6.0;

class HueSlider final : public QSlider
{
public:
    explicit HueSlider(QWidget* parent): QSlider(Qt::Vertical, parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        setSliderDown(true);
        update_value(event->position().y());
        mouse_update_timer_.restart();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!mouse_update_timer_.isValid() ||
            mouse_update_timer_.elapsed() >= color_update_interval_ms) {
            update_value(event->position().y());
            mouse_update_timer_.restart();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        update_value(event->position().y());
        setSliderDown(false);
        mouse_update_timer_.invalidate();
        event->accept();
    }

private:
    void update_value(qreal y)
    {
        const int slider_span = height() - 1;
        const int position = qBound(0, qRound(y), slider_span);
        setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), position, slider_span, true));
    }

    QElapsedTimer mouse_update_timer_;
};

class ColorSlider final : public QWidget
{
public:
    using ColorChangedCallback = std::function<void(const QColor& color)>;

    explicit ColorSlider(QWidget* parent): QWidget(parent) {}

    void set_color(const QColor& color)
    {
        hue_ = color.hsvHueF() < 0.0 ? 0.0 : color.hsvHueF();
        saturation_ = color.saturationF();
        value_ = color.valueF();
        update();
    }

    void set_hue(qreal hue)
    {
        hue_ = hue;
        update();
    }

    qreal saturation() const { return saturation_; }
    qreal value() const { return value_; }
    void set_color_changed_callback(ColorChangedCallback callback) { color_changed_callback_ = std::move(callback); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient saturation_gradient(0, 0, width(), 0);
        saturation_gradient.setColorAt(0.0, Qt::white);
        saturation_gradient.setColorAt(1.0, QColor::fromHsvF(hue_, 1.0, 1.0));
        painter.fillRect(rect(), saturation_gradient);

        QLinearGradient value_gradient(0, 0, 0, height());
        value_gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
        value_gradient.setColorAt(1.0, Qt::black);
        painter.fillRect(rect(), value_gradient);

        painter.setPen(QPen{QColor{"#666666"}, 1.0});
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        painter.setPen(QPen(value_ > 0.5 ? Qt::black : Qt::white, 3));
        painter.drawEllipse(
            QPointF(width() * saturation_, height() * (1.0 - value_)),
            color_selector_radius,
            color_selector_radius
        );
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        update_color(event->pos());
        mouse_update_timer_.restart();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!mouse_update_timer_.isValid() ||
            mouse_update_timer_.elapsed() >= color_update_interval_ms) {
            update_color(event->pos());
            mouse_update_timer_.restart();
            return;
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        update_color(event->pos());
        mouse_update_timer_.invalidate();
        event->accept();
    }

private:
    void update_color(const QPoint& position)
    {
        saturation_ = qBound(0.0, static_cast<qreal>(position.x()) / width(), 1.0);
        value_ = qBound(0.0, 1.0 - static_cast<qreal>(position.y()) / height(), 1.0);
        if (color_changed_callback_) {
            color_changed_callback_(QColor::fromHsvF(hue_, saturation_, value_));
        }
        update();
    }

    QElapsedTimer mouse_update_timer_;
    qreal hue_ = 1.0;
    qreal saturation_ = 1.0;
    qreal value_ = 1.0;
    ColorChangedCallback color_changed_callback_;
};

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
}

GarmentColorPanel::GarmentColorPanel(QWidget* parent): QWidget(parent)
{
    setObjectName("garmentColorPanel");
    setStyleSheet(
        "#garmentColorPanel {"
        "  background-color: rgba(245, 245, 245, 235);"
        "  border: 1px solid #9a9a9a;"
        "  border-radius: 4px;"
        "}"
        "#garmentColorPanel QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#garmentColorPanel #colorPanelCloseButton {"
        "  padding: 0;"
        "  background-color: #5a5a5a;"
        "  border: 1px solid #242424;"
        "  border-radius: 4px;"
        "}"
        "#garmentColorPanel #colorPanelCloseButton:hover {"
        "  background-color: #7a7a7a;"
        "}"
    );

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    color_display_ = new QLineEdit(this);
    color_display_->setAlignment(Qt::AlignCenter);
    color_display_->setFixedHeight(24);
    color_display_->setMaxLength(7);
    color_display_->setValidator(new QRegularExpressionValidator(
        QRegularExpression("^#[0-9A-Fa-f]{6}$"),
        color_display_
    ));
    color_display_->installEventFilter(this);
    auto* close_button = new QPushButton(this);
    close_button->setObjectName("colorPanelCloseButton");
    close_button->setFixedSize(close_button_size, close_button_size);
    close_button->setIcon(make_close_icon());
    close_button->setIconSize(QSize{close_icon_size, close_icon_size});
    close_button->setToolTip("Close garment color");

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(8);
    header_layout->addWidget(color_display_, 1);
    header_layout->addWidget(close_button);
    root_layout->addLayout(header_layout);

    auto* picker_layout = new QHBoxLayout();
    picker_layout->setContentsMargins(0, 0, 0, 0);
    picker_layout->setSpacing(8);
    auto* color_slider = new ColorSlider(this);
    color_slider_ = color_slider;
    color_slider_->setFixedSize(color_slider_width, color_slider_height);
    hue_slider_ = new HueSlider(this);
    hue_slider_->setRange(0, hue_slider_max);
    hue_slider_->setFixedSize(hue_slider_width, color_slider_height);
    hue_slider_->setStyleSheet(
        "QSlider::groove:vertical {"
        "  background: qlineargradient(x1:0, y1:1, x2:0, y2:0,"
        "    stop:0 #ff0000, stop:0.166 #ffff00, stop:0.333 #00ff00,"
        "    stop:0.5 #00ffff, stop:0.666 #0000ff, stop:0.833 #ff00ff, stop:1 #ff0000);"
        "  width: 24px; border: 1px solid #666666;"
        "}"
        "QSlider::handle:vertical {"
        "  background: transparent; border: 2px solid white; height: 6px; margin: 0 -3px;"
        "}"
    );
    picker_layout->addWidget(color_slider_);
    picker_layout->addWidget(hue_slider_);
    root_layout->addLayout(picker_layout);

    connect(hue_slider_, &QSlider::valueChanged, this, [this](int value) {
        auto* color_slider = static_cast<ColorSlider*>(color_slider_);
        const qreal hue = static_cast<qreal>(value) / hue_slider_max;
        color_slider->set_hue(hue);
        apply_color(QColor::fromHsvF(
            hue,
            color_slider->saturation(),
            color_slider->value()
        ));
    });
    color_slider->set_color_changed_callback([this](const QColor& color) {
        apply_color(color);
    });
    connect(close_button, &QPushButton::clicked, this, &GarmentColorPanel::close_panel);

    setVisible(false);
}

void GarmentColorPanel::edit_color(const glm::vec3& color, ColorChangedCallback color_changed_callback)
{
    color_changed_callback_ = std::move(color_changed_callback);
    color_ = QColor::fromRgbF(color.r, color.g, color.b);
    sync_picker(color_);
    update_display(color_);

    setVisible(true);
    raise();
    if (visibility_changed_callback_) {
        visibility_changed_callback_();
    }
}

void GarmentColorPanel::close_panel()
{
    if (!isVisible()) {
        return;
    }

    setVisible(false);
    color_changed_callback_ = {};
    if (visibility_changed_callback_) {
        visibility_changed_callback_();
    }
}

void GarmentColorPanel::set_visibility_changed_callback(VisibilityChangedCallback callback)
{
    visibility_changed_callback_ = std::move(callback);
}

bool GarmentColorPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == color_display_) {
        if (event->type() == QEvent::MouseButtonPress && !color_display_->hasFocus()) {
            color_display_->setFocus();
            color_display_->selectAll();
            return true;
        }
        if (event->type() == QEvent::FocusOut) {
            apply_color_text();
        } else if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                apply_color_text();
                color_display_->selectAll();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GarmentColorPanel::apply_color_text()
{
    if (!color_display_->hasAcceptableInput()) {
        update_display(color_);
        return;
    }

    const QColor color{color_display_->text()};
    sync_picker(color);
    color_display_->setText(color.name(QColor::HexRgb).toUpper());
    apply_color(color);
}

void GarmentColorPanel::apply_color(const QColor& color)
{
    if (color == color_) {
        return;
    }

    color_ = color;
    update_display(color_);
    if (color_changed_callback_) {
        color_changed_callback_({
            static_cast<float>(color_.redF()),
            static_cast<float>(color_.greenF()),
            static_cast<float>(color_.blueF()),
        });
    }
}

void GarmentColorPanel::sync_picker(const QColor& color)
{
    const qreal hue = color.hsvHueF() < 0.0 ? 0.0 : color.hsvHueF();
    static_cast<ColorSlider*>(color_slider_)->set_color(color);
    const QSignalBlocker hue_blocker(*hue_slider_);
    hue_slider_->setValue(static_cast<int>(hue * hue_slider_max));
}

void GarmentColorPanel::update_display(const QColor& color)
{
    const QString text_color = color.lightnessF() > 0.5 ? "#111111" : "#ffffff";
    color_display_->setText(color.name(QColor::HexRgb).toUpper());
    color_display_->setStyleSheet(QString(
        "background-color: %1; color: %2; border: 1px solid #666666;"
    ).arg(color.name(QColor::HexRgb), text_color));
}
