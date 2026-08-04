#include "ui/Viewport.h"

#include "app/ProjectPaths.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/GarmentCardsPanel.h"
#include "ui/GarmentColorPanel.h"
#include "ui/PlacementPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWheelEvent>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;

// Camera parameters
constexpr float default_camera_yaw = 0.75f * pi;
constexpr float default_camera_pitch = 15.0f * pi / 180.0f;
constexpr float default_camera_distance = 4.0f;

constexpr float character_camera_yaw = 0.5f * pi;
constexpr float character_camera_pitch = 10.0f * pi / 180.0f;
constexpr glm::vec3 camera_target_offset{0.0f, 0.0f, 0.0f};

// Camera control parameters
constexpr float orbit_sensitivity = 0.006f;
constexpr float max_camera_pitch = 85.0f * pi / 180.0f;
constexpr float pan_distance_scale = 0.0015f;
constexpr float wheel_delta_per_step = 120.0f;
constexpr float wheel_step_epsilon = 0.0001f;
constexpr float zoom_step_scale = 0.88f;

// scene parameters
constexpr glm::vec3 background_color{0.07f, 0.09f, 0.12f};
constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

constexpr int fps_overlay_margin = 14;
constexpr int fps_overlay_horizontal_padding = 8;
constexpr int fps_overlay_vertical_padding = 4;
constexpr int render_time_update_interval_ms = 500;

// Child UI
constexpr int panel_margin = 12;
constexpr int asset_panel_width = 390;
constexpr int asset_panel_max_height = 540;
constexpr int simulation_button_size = 40;
constexpr int simulation_button_gap = 10;
constexpr int simulation_button_count = 3;
constexpr int simulation_icon_size = 22;
constexpr int loading_spinner_size = 96;
constexpr int loading_spinner_line_count = 6;
constexpr int loading_spinner_interval_ms = 80;
constexpr int right_panel_width = 280;
constexpr int right_panel_gap = 10;

enum class ControlIcon
{
    Play,
    Pause,
    DefaultPose,
    Reset,
};

class LoadingOverlay final : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    bool event(QEvent* event) override
    {
        if (!event->isInputEvent()) {
            return QLabel::event(event);
        }
        event->accept();
        return true;
    }
};

QIcon make_simulation_control_icon(ControlIcon icon_type, const QColor& icon_color)
{
    QPixmap pixmap(simulation_icon_size, simulation_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(icon_color);

    switch (icon_type) {
    case ControlIcon::Play: {
        QPolygonF triangle;
        triangle << QPointF{6.0, 4.0} << QPointF{6.0, 18.0} << QPointF{18.0, 11.0};
        painter.drawPolygon(triangle);
        break;
    }
    case ControlIcon::Pause:
        painter.drawRect(QRectF{6.0, 4.0, 4.5, 14.0});
        painter.drawRect(QRectF{13.5, 4.0, 4.5, 14.0});
        break;
    case ControlIcon::DefaultPose: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{icon_color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawArc(QRectF{4.0, 3.5, 14.0, 14.0}, -35 * 16, 285 * 16);

        painter.setPen(Qt::NoPen);
        painter.setBrush(icon_color);
        QPolygonF arrow;
        arrow << QPointF{4.2, 7.0} << QPointF{4.2, 2.5} << QPointF{8.6, 6.8};
        painter.drawPolygon(arrow);
        break;
    }
    case ControlIcon::Reset:
        painter.drawRect(QRectF{5.0, 5.0, 12.0, 12.0});
        break;
    }

    return QIcon{pixmap};
}

QPixmap make_loading_spinner_pixmap(int step)
{
    QPixmap pixmap(loading_spinner_size, loading_spinner_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(loading_spinner_size * 0.5, loading_spinner_size * 0.5);

    for (int index = 0; index < loading_spinner_line_count; ++index) {
        QColor color{"#f5f5f5"};
        color.setAlpha(55 + ((index + step) % loading_spinner_line_count) * 40);
        painter.setPen(QPen{color, 4.0, Qt::SolidLine, Qt::RoundCap});
        painter.drawLine(QPointF{0.0, -20.0}, QPointF{0.0, -38.0});
        painter.rotate(360.0 / loading_spinner_line_count);
    }

    return pixmap;
}

void setup_control_button(QPushButton* button,
                          ControlIcon icon_type,
                          const char* tool_tip,
                          const QColor& icon_color)
{
    button->setFixedSize(simulation_button_size, simulation_button_size);
    button->setIcon(make_simulation_control_icon(icon_type, icon_color));
    button->setIconSize(QSize{simulation_icon_size, simulation_icon_size});
    button->setToolTip(tool_tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(R"(
        QPushButton {
            background-color: #eeeeee;
            border: 1px solid #c8c8c8;
            border-radius: 7px;
        }
        QPushButton:hover {
            background-color: #f7f7f7;
        }
        QPushButton:disabled {
            background-color: #dddddd;
            border-color: #c6c6c6;
        }
    )");
}

void orbit_camera(OrbitCamera& camera, const QPoint& delta)
{
    camera.yaw_radians -= static_cast<float>(delta.x()) * orbit_sensitivity;
    camera.pitch_radians += static_cast<float>(delta.y()) * orbit_sensitivity;
    camera.pitch_radians = std::clamp(camera.pitch_radians, -max_camera_pitch, max_camera_pitch);
}

void pan_camera(OrbitCamera& camera, const QPoint& delta, const glm::vec3& eye)
{
    const glm::vec3 forward = glm::normalize(camera.target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
    const glm::vec3 up = glm::cross(right, forward);
    const float pan_scale = camera.distance * pan_distance_scale;

    camera.target +=
        right * static_cast<float>(-delta.x() * pan_scale) + up * static_cast<float>(delta.y() * pan_scale);
}

glm::vec3 camera_position(const OrbitCamera& camera)
{
    const float cos_pitch = std::cos(camera.pitch_radians);
    return {
        camera.target.x + camera.distance * cos_pitch * std::cos(camera.yaw_radians),
        camera.target.y + camera.distance * std::sin(camera.pitch_radians),
        camera.target.z + camera.distance * cos_pitch * std::sin(camera.yaw_radians),
    };
}

glm::mat4 make_mvp(const OrbitCamera& camera, int width, int height)
{
    const int viewport_width = std::max(1, width);
    const int viewport_height = std::max(1, height);
    const float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
    const float near_plane = std::max(0.01f, camera.distance * 0.01f);
    const float far_plane = std::max(100.0f, camera.max_distance * 4.0f);
    const glm::mat4 projection = glm::perspective(45.0f * pi / 180.0f, aspect, near_plane, far_plane);
    const glm::mat4 view = glm::lookAt(camera_position(camera), camera.target, world_up);
    return projection * view;
}

}

Viewport::Viewport(const ProjectPaths& project_paths, QWidget* parent) : QOpenGLWidget(parent)
{
    camera_.yaw_radians = default_camera_yaw;
    camera_.pitch_radians = default_camera_pitch;
    camera_.distance = default_camera_distance;

    asset_browser_panel_ = new AssetBrowserPanel(project_paths, this);
    placement_panel_ = new PlacementPanel(this);
    garment_color_panel_ = new GarmentColorPanel(this);
    garment_cards_panel_ = new GarmentCardsPanel(this);

    play_pause_button_ = new QPushButton(this);
    default_pose_button_ = new QPushButton(this);
    reset_button_ = new QPushButton(this);
    setup_control_button(play_pause_button_, ControlIcon::Play, "Run", QColor{"#43a047"});
    setup_control_button(default_pose_button_, ControlIcon::DefaultPose, "Default Pose", QColor{"#1e88e5"});
    setup_control_button(reset_button_, ControlIcon::Reset, "Reset", QColor{"#e53935"});
    setup_loading_overlay();

    connect(play_pause_button_, &QPushButton::clicked, this, [this]() {
        if (play_pause_callback_) {
            play_pause_callback_();
        }
    });
    connect(default_pose_button_, &QPushButton::clicked, this, [this]() {
        if (default_pose_callback_) {
            default_pose_callback_();
        }
    });
    connect(reset_button_, &QPushButton::clicked, this, [this]() {
        if (reset_callback_) {
            reset_callback_();
        }
    });
    asset_browser_panel_->set_expansion_changed_callback([this]() { update_layout(); });

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    update_layout();
}

Viewport::~Viewport() = default;

// Child UI //
AssetBrowserPanel& Viewport::asset_browser_panel()
{
    return *asset_browser_panel_;
}

PlacementPanel& Viewport::placement_panel()
{
    return *placement_panel_;
}

GarmentColorPanel& Viewport::garment_color_panel()
{
    return *garment_color_panel_;
}

GarmentCardsPanel& Viewport::garment_cards_panel()
{
    return *garment_cards_panel_;
}

void Viewport::set_play_pause_callback(std::function<void()> callback)
{
    play_pause_callback_ = std::move(callback);
}

void Viewport::set_default_pose_callback(std::function<void()> callback)
{
    default_pose_callback_ = std::move(callback);
}

void Viewport::set_reset_callback(std::function<void()> callback)
{
    reset_callback_ = std::move(callback);
}

void Viewport::set_simulation_button_state(bool simulation_running, bool buttons_enabled)
{
    play_pause_button_->setIcon(
        make_simulation_control_icon(simulation_running ? ControlIcon::Pause : ControlIcon::Play,
                                     simulation_running ? QColor{"#f4b400"} : QColor{"#43a047"}));
    play_pause_button_->setToolTip(simulation_running ? "Pause" : "Run");
    play_pause_button_->setEnabled(buttons_enabled);
    default_pose_button_->setEnabled(buttons_enabled);
}

void Viewport::setup_loading_overlay()
{
    loading_overlay_ = new LoadingOverlay(this);
    loading_overlay_->setAlignment(Qt::AlignCenter);
    loading_overlay_->setFocusPolicy(Qt::StrongFocus);
    loading_overlay_->setPixmap(make_loading_spinner_pixmap(loading_spinner_step_));
    loading_overlay_->setVisible(false);

    loading_spinner_timer_ = new QTimer(this);
    loading_spinner_timer_->setInterval(loading_spinner_interval_ms);
    connect(loading_spinner_timer_, &QTimer::timeout, this, [this]() {
        loading_spinner_step_ = (loading_spinner_step_ + 1) % loading_spinner_line_count;
        loading_overlay_->setPixmap(make_loading_spinner_pixmap(loading_spinner_step_));
    });
}

void Viewport::set_loading_overlay_active(bool active)
{
    loading_overlay_->setVisible(active);
    if (active) {
        loading_overlay_->setFocus(Qt::OtherFocusReason);
        loading_spinner_timer_->start();
        loading_overlay_->raise();
    } else {
        loading_spinner_timer_->stop();
        setFocus(Qt::OtherFocusReason);
    }
}

void Viewport::update_layout()
{
    update_asset_browser_layout();
    update_simulation_control_button_layout();
    update_right_panel_layout();
    update_loading_overlay_layout();
}

void Viewport::update_asset_browser_layout()
{
    const QSize collapsed_size = asset_browser_panel_->sizeHint();

    int panel_width = collapsed_size.width();
    int panel_height = collapsed_size.height();

    if (asset_browser_panel_->is_expanded()) {
        panel_width = asset_panel_width;
        panel_height = std::min(height() / 2, asset_panel_max_height);
    }

    asset_browser_panel_->setGeometry(panel_margin, panel_margin, panel_width, panel_height);
    asset_browser_panel_->raise();
}

void Viewport::update_simulation_control_button_layout()
{
    constexpr int button_group_width = simulation_button_size * simulation_button_count +
                                       simulation_button_gap * (simulation_button_count - 1);
    constexpr int button_x_offset = simulation_button_size + simulation_button_gap;

    const int button_group_left = (width() - button_group_width) / 2;

    play_pause_button_->move(button_group_left, panel_margin);
    default_pose_button_->move(button_group_left + button_x_offset, panel_margin);
    reset_button_->move(button_group_left + button_x_offset * 2, panel_margin);

    play_pause_button_->raise();
    default_pose_button_->raise();
    reset_button_->raise();
}

void Viewport::update_right_panel_layout()
{
    // Right panels { PlacementPanel, GarmentCardsPanel, GarmentColorPanel }
    const int panel_left = width() - panel_margin - right_panel_width;
    const int panel_bottom = height() - panel_margin;

    const std::array<QWidget*, 3> panels{
        placement_panel_,
        garment_cards_panel_,
        garment_color_panel_,
    };

    int panel_top = panel_margin + simulation_button_size + right_panel_gap;
    for (QWidget* panel : panels) {
        if (!panel->isVisible()) {
            continue;
        }

        const int remaining_height = std::max(0, panel_bottom - panel_top);
        const int panel_height = std::min(panel->sizeHint().height(), remaining_height);
        panel->setGeometry(panel_left, panel_top, right_panel_width, panel_height);
        panel->raise();
        panel_top += panel_height + right_panel_gap;
    }
}

void Viewport::update_loading_overlay_layout()
{
    loading_overlay_->setGeometry(rect());
    loading_overlay_->raise();
}

// Accessors //
void Viewport::set_initialize_callback(InitializeCallback callback)
{
    initialize_callback_ = std::move(callback);
}

void Viewport::set_scene_render_callback(SceneRenderCallback callback)
{
    scene_render_callback_ = std::move(callback);
}

QOpenGLFunctions_4_5_Core& Viewport::gl_functions()
{
    return *this;
}

// OpenGL //
void Viewport::initializeGL()
{
    initializeOpenGLFunctions();

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    if (!initialize_callback_) {
        std::cerr << "Scene initialize callback is not set before OpenGL initialization.\n";
        return;
    }

    // Simulation_Controller::initialize_gpu
    if (!initialize_callback_(gl_functions())) {
        return;
    }

    // Render state initialization
    glEnable(GL_DEPTH_TEST);
}

void Viewport::resizeGL(int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
    update_layout();
}

void Viewport::paintGL()
{
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (scene_render_callback_) {
        // MVP 계산 -> shader uMVP로 전달
        const glm::mat4 mvp = make_mvp(camera_, width(), height());
        scene_render_callback_(mvp, gl_functions());
    }

    update_render_time();
    draw_display_fps();
}

void Viewport::update_render_time()
{
    if (!render_fps_timer_.isValid()) {
        render_fps_timer_.start();
    }

    ++render_frame_count_;

    const qint64 elapsed_ms = render_fps_timer_.elapsed();
    if (elapsed_ms < render_time_update_interval_ms) {
        return;
    }

    render_fps_ = static_cast<double>(render_frame_count_) * 1000.0 / static_cast<double>(elapsed_ms);
    frame_ms_ = static_cast<double>(elapsed_ms) / static_cast<double>(render_frame_count_);
    render_frame_count_ = 0;
    render_fps_timer_.restart();
}

void Viewport::draw_display_fps()
{
    const QString fps_text =
        QString("FPS: %1  Frame: %2ms").arg(render_fps_, 0, 'f', 1).arg(frame_ms_, 0, 'f', 1);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QFontMetrics metrics(painter.font());
    const QRect text_bounds = metrics.boundingRect(fps_text);
    QRect background_rect(0,
                          0,
                          text_bounds.width() + fps_overlay_horizontal_padding * 2,
                          text_bounds.height() + fps_overlay_vertical_padding * 2);
    background_rect.moveBottomRight(QPoint(width() - fps_overlay_margin, height() - fps_overlay_margin));

    painter.fillRect(background_rect, QColor(0, 0, 0, 150));
    painter.setPen(QColor(235, 240, 245));
    painter.drawText(background_rect.adjusted(fps_overlay_horizontal_padding,
                                              fps_overlay_vertical_padding,
                                              -fps_overlay_horizontal_padding,
                                              -fps_overlay_vertical_padding),
                     Qt::AlignCenter,
                     fps_text);
}

// Camera //
// 현재 character root 기준으로 camera 초기화
void Viewport::reset_camera(const glm::vec3& root_position)
{
    camera_.target = root_position + camera_target_offset;
    camera_.yaw_radians = character_camera_yaw;
    camera_.pitch_radians = character_camera_pitch;
    camera_.distance = default_camera_distance;
    camera_.min_distance = 0.25f;
    camera_.max_distance = 50.0f;
    camera_.has_last_mouse = false;
}

void Viewport::set_camera_target(const glm::vec3& root_position)
{
    const glm::vec3 next_target = root_position + camera_target_offset;
    camera_.target.x = next_target.x;
    camera_.target.z = next_target.z;
}

// Mouse Event //
void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        event->ignore();
        return;
    }

    camera_.last_mouse_position = event->pos();
    camera_.has_last_mouse = true;
    event->accept();
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (!camera_.has_last_mouse) {
        event->ignore();
        return;
    }

    const QPoint delta = event->pos() - camera_.last_mouse_position;
    camera_.last_mouse_position = event->pos();

    if (event->buttons() & Qt::RightButton) {
        orbit_camera(camera_, delta);
    } else if (event->buttons() & Qt::MiddleButton) {
        pan_camera(camera_, delta, camera_position(camera_));
    } else {
        event->ignore();
        return;
    }

    event->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons() == Qt::NoButton) {
        camera_.has_last_mouse = false;
    }
    event->accept();
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const float wheel_steps = static_cast<float>(event->angleDelta().y()) / wheel_delta_per_step;
    if (std::abs(wheel_steps) > wheel_step_epsilon) {
        camera_.distance *= std::pow(zoom_step_scale, wheel_steps);
        camera_.distance = std::clamp(camera_.distance, camera_.min_distance, camera_.max_distance);
    }
    event->accept();
}
