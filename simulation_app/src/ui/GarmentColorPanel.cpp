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
constexpr int color_picker_width = 226;
constexpr int color_picker_height = 150;
constexpr int hue_slider_width = 26;
constexpr qreal color_selector_radius = 6.0;

const QString panel_style = QStringLiteral(R"(
    #garmentColorPanel {
        background-color: rgba(245, 245, 245, 235);
        border: 1px solid #9a9a9a;
        border-radius: 4px;
    }
    #garmentColorPanel QLineEdit {
        background: transparent;
        border: none;
    }
    #garmentColorPanel #colorPanelCloseButton {
        padding: 0;
        background-color: #5a5a5a;
        border: 1px solid #242424;
        border-radius: 4px;
    }
    #garmentColorPanel #colorPanelCloseButton:hover {
        background-color: #7a7a7a;
    }
)");

const QString hue_slider_style = QStringLiteral(R"(
    QSlider::groove:vertical {
        background: qlineargradient(x1:0, y1:1, x2:0, y2:0,
            stop:0 #ff0000, stop:0.166 #ffff00, stop:0.333 #00ff00,
            stop:0.5 #00ffff, stop:0.666 #0000ff, stop:0.833 #ff00ff, stop:1 #ff0000);
        width: 24px;
        border: 1px solid #666666;
    }
    QSlider::handle:vertical {
        background: transparent;
        border: 2px solid white;
        height: 6px;
        margin: 0 -3px;
    }
)");

const QString color_display_style = QStringLiteral("background-color: %1; color: %2; "
                                                   "border: 1px solid #666666;");

class HueSlider final : public QSlider
{
public:
    explicit HueSlider(QWidget* parent) : QSlider(Qt::Vertical, parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }

        setSliderDown(true);
        update_value(event->position().y());
        mouse_update_timer_.start();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QSlider::mouseMoveEvent(event);
            return;
        }

        if (mouse_update_timer_.elapsed() >= color_update_interval_ms) {
            update_value(event->position().y());
            mouse_update_timer_.restart();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mouseReleaseEvent(event);
            return;
        }

        update_value(event->position().y());
        setSliderDown(false);
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
}

class GarmentColorPanel::SaturationValuePicker final : public QWidget
{
public:
    explicit SaturationValuePicker(GarmentColorPanel& panel) : QWidget(&panel), panel_(panel) {}

    void set_color(const QColor& color)
    {
        hue_ = qMax(0.0, color.hsvHueF());
        saturation_ = color.saturationF();
        value_ = color.valueF();
        update();
    }

    QColor color() const { return QColor::fromHsvF(hue_, saturation_, value_); }

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
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        painter.setPen(QPen(value_ > 0.5 ? Qt::black : Qt::white, 3));
        painter.drawEllipse(QPointF((width() - 1) * saturation_, (height() - 1) * (1.0 - value_)),
                            color_selector_radius,
                            color_selector_radius);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        update_color(event->pos());
        mouse_update_timer_.start();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (mouse_update_timer_.elapsed() >= color_update_interval_ms) {
            update_color(event->pos());
            mouse_update_timer_.restart();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        update_color(event->pos());
        event->accept();
    }

private:
    void update_color(const QPoint& position)
    {
        const int horizontal_span = width() - 1;
        const int vertical_span = height() - 1;
        saturation_ = qBound(0.0, static_cast<qreal>(position.x()) / horizontal_span, 1.0);
        value_ = qBound(0.0, 1.0 - static_cast<qreal>(position.y()) / vertical_span, 1.0);
        panel_.apply_color(color());
    }

    GarmentColorPanel& panel_;
    QElapsedTimer mouse_update_timer_;
    qreal hue_ = 1.0;
    qreal saturation_ = 1.0;
    qreal value_ = 1.0;
};

GarmentColorPanel::GarmentColorPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName("garmentColorPanel");
    setStyleSheet(panel_style);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 10);
    root_layout->setSpacing(8);

    setup_header(*root_layout);
    setup_picker(*root_layout);

    setVisible(false);
}

// Initialization
void GarmentColorPanel::setup_header(QVBoxLayout& root_layout)
{
    const QRegularExpression color_pattern("#[0-9A-Fa-f]{6}");
    color_hex_ = new QLineEdit(this);
    color_hex_->setAlignment(Qt::AlignCenter);
    color_hex_->setFixedHeight(24);
    color_hex_->setMaxLength(7);
    color_hex_->setValidator(new QRegularExpressionValidator(color_pattern, color_hex_));
    color_hex_->installEventFilter(this);

    auto* close_button = new QPushButton(this);
    close_button->setObjectName("colorPanelCloseButton");
    close_button->setFixedSize(close_button_size, close_button_size);
    close_button->setIcon(QIcon{QStringLiteral(":/icons/close.svg")});
    close_button->setIconSize(QSize{close_icon_size, close_icon_size});
    close_button->setToolTip("Close garment color");

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->addWidget(color_hex_, 1);
    header_layout->addWidget(close_button);
    root_layout.addLayout(header_layout);

    connect(close_button, &QPushButton::clicked, this, &GarmentColorPanel::close_panel);
}

void GarmentColorPanel::setup_picker(QVBoxLayout& root_layout)
{
    saturation_value_picker_ = new SaturationValuePicker(*this);
    saturation_value_picker_->setFixedSize(color_picker_width, color_picker_height);

    hue_slider_ = new HueSlider(this);
    hue_slider_->setRange(0, hue_slider_max);
    hue_slider_->setFixedSize(hue_slider_width, color_picker_height);
    hue_slider_->setStyleSheet(hue_slider_style);

    auto* picker_layout = new QHBoxLayout();
    picker_layout->setContentsMargins(0, 0, 0, 0);
    picker_layout->setSpacing(8);
    picker_layout->addWidget(saturation_value_picker_);
    picker_layout->addWidget(hue_slider_);

    root_layout.addLayout(picker_layout);

    connect(hue_slider_, &QSlider::valueChanged, this, [this](int value) {
        const qreal hue = static_cast<qreal>(value) / hue_slider_max;
        apply_color(QColor::fromHsvF(hue, color_.saturationF(), color_.valueF()));
    });
}

// UI Updates
void GarmentColorPanel::open_panel(const glm::vec3& color)
{
    set_color(QColor::fromRgbF(color.r, color.g, color.b));

    if (!isVisible()) {
        setVisible(true);
        Q_EMIT visibility_changed(true);
    }
}

void GarmentColorPanel::close_panel()
{
    if (!isVisible()) {
        return;
    }

    setVisible(false);
    Q_EMIT visibility_changed(false);
}

// Color Interaction
bool GarmentColorPanel::eventFilter(QObject*, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && !color_hex_->hasFocus()) {
            color_hex_->setFocus();
            color_hex_->selectAll();
            return true;
        }
        break;
    case QEvent::FocusOut:
        apply_color_hex();
        break;
    case QEvent::KeyPress:
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            apply_color_hex();
            color_hex_->selectAll();
            return true;
        }
        break;
    }

    return false;
}

void GarmentColorPanel::apply_color_hex()
{
    if (!color_hex_->hasAcceptableInput()) {
        set_color(color_);
        return;
    }

    apply_color(QColor{color_hex_->text()});
}

void GarmentColorPanel::apply_color(const QColor& color)
{
    set_color(color);
    Q_EMIT color_changed({
        static_cast<float>(color_.redF()),
        static_cast<float>(color_.greenF()),
        static_cast<float>(color_.blueF()),
    });
}

void GarmentColorPanel::set_color(const QColor& color)
{
    color_ = color;
    saturation_value_picker_->set_color(color);

    const qreal hue = qMax(0.0, color.hsvHueF());
    const QSignalBlocker hue_blocker(*hue_slider_);
    hue_slider_->setValue(static_cast<int>(hue * hue_slider_max));

    const QString color_name = color.name().toUpper();
    const QString text_color = qGray(color.rgb()) >= 128 ? "#111111" : "#ffffff";
    color_hex_->setText(color_name);
    color_hex_->setStyleSheet(color_display_style.arg(color_name, text_color));
}
