#include "ui/Viewport.h"

#include "app/ProjectPaths.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/GarmentCardsPanel.h"
#include "ui/GarmentColorPanel.h"
#include "ui/PlacementPanel.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
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

// Camera control parameters
constexpr float orbit_sensitivity = 0.006f;
constexpr float max_camera_pitch = 85.0f * pi / 180.0f;
constexpr float pan_distance_scale = 0.0015f;
constexpr float zoom_step_scale = 0.88f;

// scene parameters
constexpr glm::vec3 background_color{0.07f, 0.09f, 0.12f};
constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

constexpr int frame_stats_margin = 14;
constexpr QSize frame_stats_size{200, 28};
constexpr int frame_stats_update_interval_ms = 500;

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
constexpr int right_panel_content_width = 280;
constexpr int right_panel_gap = 10;
constexpr int right_panel_bottom_margin = 50;

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

void setup_control_button(QPushButton* button, const QIcon& icon, const char* tool_tip)
{
    button->setFixedSize(simulation_button_size, simulation_button_size);
    button->setIcon(icon);
    button->setIconSize(QSize{simulation_icon_size, simulation_icon_size});
    button->setToolTip(tool_tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty("role", "viewportControl");
}

bool is_camera_control_button(Qt::MouseButtons buttons)
{
    return buttons.testFlag(Qt::RightButton) || buttons.testFlag(Qt::MiddleButton);
}

void orbit_camera(OrbitCamera& camera, const QPoint& delta)
{
    camera.yaw_radians -= delta.x() * orbit_sensitivity;
    const float next_pitch = camera.pitch_radians + delta.y() * orbit_sensitivity;
    camera.pitch_radians = std::clamp(next_pitch, -max_camera_pitch, max_camera_pitch);
}

void pan_camera(OrbitCamera& camera, const QPoint& delta, const glm::vec3& eye)
{
    const glm::vec3 forward = glm::normalize(camera.target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
    const glm::vec3 up = glm::cross(right, forward);
    const float pan_scale = camera.distance * pan_distance_scale;

    camera.target += right * (-delta.x() * pan_scale) + up * (delta.y() * pan_scale);
}

glm::vec3 camera_position(const OrbitCamera& camera)
{
    const float cos_pitch = std::cos(camera.pitch_radians);
    const glm::vec3 orbit_direction{
        cos_pitch * std::cos(camera.yaw_radians),
        std::sin(camera.pitch_radians),
        cos_pitch * std::sin(camera.yaw_radians),
    };

    return camera.target + camera.distance * orbit_direction;
}

}

Viewport::Viewport(const ProjectPaths& project_paths, QWidget* parent) : QOpenGLWidget(parent)
{
    camera_.yaw_radians = default_camera_yaw;
    camera_.pitch_radians = default_camera_pitch;
    camera_.distance = default_camera_distance;

    setup_panels(project_paths);
    setup_simulation_control_buttons();
    setup_loading_overlay();

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    update_layout();
}

Viewport::~Viewport() = default;

// Initialization
void Viewport::setup_panels(const ProjectPaths& project_paths)
{
    asset_browser_panel_ = new AssetBrowserPanel(project_paths, this);
    placement_panel_ = new PlacementPanel(this);
    garment_color_panel_ = new GarmentColorPanel(this);
    garment_cards_panel_ = new GarmentCardsPanel(this);

    auto* panel_content = new QWidget;
    panel_content->setFixedWidth(right_panel_content_width);

    auto* layout = new QVBoxLayout(panel_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(right_panel_gap);
    layout->addWidget(placement_panel_);
    layout->addWidget(garment_cards_panel_);
    layout->addWidget(garment_color_panel_);

    right_panel_ = new QScrollArea(this);
    right_panel_->setWidget(panel_content);
    panel_content->setAutoFillBackground(false);
    right_panel_->setFrameShape(QFrame::NoFrame);
    right_panel_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    right_panel_->viewport()->setAutoFillBackground(false);

    connect(asset_browser_panel_, &AssetBrowserPanel::expansion_changed, this, &Viewport::update_layout);
}

void Viewport::setup_simulation_control_buttons()
{
    play_pause_button_ = new QPushButton(this);
    default_pose_button_ = new QPushButton(this);
    reset_button_ = new QPushButton(this);
    setup_control_button(play_pause_button_, play_icon_, "Run");
    setup_control_button(default_pose_button_,
                         QIcon{QStringLiteral(":/icons/default-pose.svg")},
                         "Return to Default Pose");
    setup_control_button(reset_button_, QIcon{QStringLiteral(":/icons/reset-scene.svg")}, "Reset Scene");

    connect(play_pause_button_, &QPushButton::clicked, this, &Viewport::play_pause_requested);
    connect(default_pose_button_, &QPushButton::clicked, this, &Viewport::default_pose_requested);
    connect(reset_button_, &QPushButton::clicked, this, &Viewport::reset_requested);
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

void Viewport::initializeGL()
{
    initializeOpenGLFunctions();

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    if (!initialize_callback_) {
        std::cerr << "Scene initialize callback is not set before OpenGL initialization.\n";
        return;
    }

    if (!initialize_callback_(gl_functions())) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
}

// UI Updates
void Viewport::set_simulation_button_state(bool simulation_running, bool buttons_enabled)
{
    play_pause_button_->setIcon(simulation_running ? pause_icon_ : play_icon_);
    play_pause_button_->setToolTip(simulation_running ? "Pause" : "Run");
    play_pause_button_->setEnabled(buttons_enabled);
    default_pose_button_->setEnabled(buttons_enabled);
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
    const bool has_any_visible_panel = !placement_panel_->isHidden() ||
                                       !garment_cards_panel_->isHidden() ||
                                       !garment_color_panel_->isHidden();
    right_panel_->setVisible(has_any_visible_panel);
    if (!has_any_visible_panel) {
        return;
    }

    QWidget* panel_content = right_panel_->widget();
    panel_content->adjustSize();

    constexpr int panel_top = panel_margin + simulation_button_size + right_panel_gap;
    const int content_height = panel_content->height();
    const int panel_height = std::min(content_height, height() - panel_top - right_panel_bottom_margin);
    const bool needs_scrollbar = content_height > panel_height;
    const int scrollbar_width = needs_scrollbar ? style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
    const int panel_width = right_panel_content_width + scrollbar_width;
    const int panel_left = width() - panel_margin - panel_width;

    right_panel_->setGeometry(panel_left, panel_top, panel_width, panel_height);
    right_panel_->raise();
}

void Viewport::update_loading_overlay_layout()
{
    loading_overlay_->setGeometry(rect());
    loading_overlay_->raise();
}

// Rendering
void Viewport::resizeGL(int width, int height)
{
    update_layout();
}

void Viewport::paintGL()
{
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 mvp = make_mvp();
    scene_render_callback_(mvp, gl_functions());

    update_frame_stats();
    draw_frame_stats();
}

glm::mat4 Viewport::make_mvp() const
{
    const float aspect = static_cast<float>(std::max(1, width())) / std::max(1, height());
    const float near_plane = std::max(0.01f, camera_.distance * 0.01f);
    const float far_plane = std::max(100.0f, camera_.max_distance * 4.0f);
    const glm::mat4 projection = glm::perspective(45.0f * pi / 180.0f, aspect, near_plane, far_plane);
    const glm::mat4 view = glm::lookAt(camera_position(camera_), camera_.target, world_up);
    return projection * view;
}

void Viewport::update_frame_stats()
{
    if (!render_fps_timer_.isValid()) {
        render_fps_timer_.start();
    }

    ++render_frame_count_;

    const double elapsed_ms = static_cast<double>(render_fps_timer_.elapsed());
    if (elapsed_ms < frame_stats_update_interval_ms) {
        return;
    }

    render_fps_ = render_frame_count_ * 1000.0 / elapsed_ms;
    frame_ms_ = elapsed_ms / render_frame_count_;
    render_frame_count_ = 0;
    render_fps_timer_.restart();
}

void Viewport::draw_frame_stats()
{
    const QString stats_text =
        QStringLiteral("FPS: %1  Frame time: %2 ms").arg(render_fps_, 0, 'f', 1).arg(frame_ms_, 0, 'f', 1);

    QPainter painter(this);

    QRect background_rect{QPoint{}, frame_stats_size};
    background_rect.moveBottomRight(QPoint(width() - frame_stats_margin, height() - frame_stats_margin));

    painter.fillRect(background_rect, QColor(0, 0, 0, 150));
    painter.setPen(QColor(235, 240, 245));
    painter.drawText(background_rect, Qt::AlignCenter, stats_text);
}

// Camera
void Viewport::reset_camera(const glm::vec3& root_position)
{
    camera_.target = root_position;
    camera_.yaw_radians = character_camera_yaw;
    camera_.pitch_radians = character_camera_pitch;
    camera_.distance = default_camera_distance;
    camera_.is_dragging = false;
}

void Viewport::set_camera_target(const glm::vec3& root_position)
{
    camera_.target.x = root_position.x;
    camera_.target.z = root_position.z;
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (!is_camera_control_button(event->button())) {
        event->ignore();
        return;
    }

    camera_.last_mouse_position = event->pos();
    camera_.is_dragging = true;
    event->accept();
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    const Qt::MouseButtons buttons = event->buttons();
    if (!camera_.is_dragging || !is_camera_control_button(buttons)) {
        event->ignore();
        return;
    }

    const QPoint current_position = event->pos();
    const QPoint delta = current_position - camera_.last_mouse_position;
    camera_.last_mouse_position = current_position;

    if (buttons & Qt::RightButton) {
        orbit_camera(camera_, delta);
    } else {
        pan_camera(camera_, delta, camera_position(camera_));
    }

    event->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (!is_camera_control_button(event->buttons())) {
        camera_.is_dragging = false;
    }
    event->accept();
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const int wheel_delta = event->angleDelta().y();
    if (wheel_delta != 0) {
        const float wheel_steps = static_cast<float>(wheel_delta) / QWheelEvent::DefaultDeltasPerStep;
        const float scaled_distance = camera_.distance * std::pow(zoom_step_scale, wheel_steps);
        camera_.distance = std::clamp(scaled_distance, camera_.min_distance, camera_.max_distance);
    }
    event->accept();
}

// Accessors
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

QOpenGLFunctions_4_5_Core& Viewport::gl_functions()
{
    return *this;
}

// Callback Registration
void Viewport::set_initialize_callback(InitializeCallback callback)
{
    initialize_callback_ = std::move(callback);
}

void Viewport::set_scene_render_callback(SceneRenderCallback callback)
{
    scene_render_callback_ = std::move(callback);
}
