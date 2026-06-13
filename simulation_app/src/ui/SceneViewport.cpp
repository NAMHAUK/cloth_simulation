#include "ui/SceneViewport.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QString>
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

constexpr float character_camera_distance_min = 1.5f;
constexpr float character_camera_distance_scale = 3.0f;

constexpr float character_camera_near_min = 0.05f;
constexpr float character_camera_near_scale = 0.15f;

constexpr float character_camera_far_min = 10.0f;
constexpr float character_camera_far_scale = 12.0f;

// Camera control parameters
constexpr float orbit_sensitivity    = 0.006f;
constexpr float max_camera_pitch     = 85.0f * pi / 180.0f;
constexpr float pan_distance_scale   = 0.0015f;
constexpr float wheel_delta_per_step = 120.0f;
constexpr float wheel_step_epsilon   = 0.0001f;
constexpr float zoom_step_scale      = 0.88f;

// scene parameters
constexpr glm::vec3 background_color{0.07f, 0.09f, 0.12f};
constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

constexpr int fps_overlay_margin = 14;
constexpr int fps_overlay_horizontal_padding = 8;
constexpr int fps_overlay_vertical_padding = 4;
constexpr int render_time_update_interval_ms = 500;

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
        right * static_cast<float>(-delta.x() * pan_scale) +
        up * static_cast<float>(delta.y() * pan_scale);
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

SceneViewport::SceneViewport(QWidget* parent) : QOpenGLWidget(parent)
{
    camera_.yaw_radians   = default_camera_yaw;
    camera_.pitch_radians = default_camera_pitch;
    camera_.distance      = default_camera_distance;

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

SceneViewport::~SceneViewport() = default;

// Accessors //
void SceneViewport::set_initialize_callback(InitializeCallback callback)
{
    initialize_callback_ = std::move(callback);
}

void SceneViewport::set_scene_render_callback(SceneRenderCallback callback)
{
    scene_render_callback_ = std::move(callback);
}

bool SceneViewport::is_gl_initialized() const
{
    return gl_initialized_;
}

QOpenGLFunctions_4_5_Core& SceneViewport::gl_functions()
{
    return *this;
}

// OpenGL //
void SceneViewport::initializeGL()
{
    initializeOpenGLFunctions();
    gl_initialized_ = true;

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

void SceneViewport::resizeGL(int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void SceneViewport::paintGL()
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

void SceneViewport::update_render_time()
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

void SceneViewport::draw_display_fps()
{
    const QString fps_text = QString("FPS: %1  Frame: %2ms")
        .arg(render_fps_, 0, 'f', 1)
        .arg(frame_ms_, 0, 'f', 1);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QFontMetrics metrics(painter.font());
    const QRect text_bounds = metrics.boundingRect(fps_text);
    QRect background_rect(
        0,
        0,
        text_bounds.width() + fps_overlay_horizontal_padding * 2,
        text_bounds.height() + fps_overlay_vertical_padding * 2
    );
    background_rect.moveBottomRight(QPoint(width() - fps_overlay_margin, height() - fps_overlay_margin));

    painter.fillRect(background_rect, QColor(0, 0, 0, 150));
    painter.setPen(QColor(235, 240, 245));
    painter.drawText(
        background_rect.adjusted(
            fps_overlay_horizontal_padding,
            fps_overlay_vertical_padding,
            -fps_overlay_horizontal_padding,
            -fps_overlay_vertical_padding
        ),
        Qt::AlignCenter,
        fps_text
    );
}

// Camera //
// 현재 motion character bounds 기준으로 camera 초기화
void SceneViewport::reset_camera_to_character(const CharacterMesh& character_mesh)
{
    camera_.target = character_mesh.bounds_center;
    camera_.yaw_radians = character_camera_yaw;
    camera_.pitch_radians = character_camera_pitch;
    camera_.distance = std::max(character_camera_distance_min, character_mesh.bounds_radius * character_camera_distance_scale);
    camera_.min_distance = std::max(character_camera_near_min, character_mesh.bounds_radius * character_camera_near_scale);
    camera_.max_distance = std::max(character_camera_far_min, character_mesh.bounds_radius * character_camera_far_scale);
    camera_.has_last_mouse = false;
}

// Mouse Event //
void SceneViewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        event->ignore();
        return;
    }

    camera_.last_mouse_position = event->pos();
    camera_.has_last_mouse = true;
    event->accept();
}

void SceneViewport::mouseMoveEvent(QMouseEvent* event)
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

void SceneViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons() == Qt::NoButton) {
        camera_.has_last_mouse = false;
    }
    event->accept();
}

void SceneViewport::wheelEvent(QWheelEvent* event)
{
    const float wheel_steps = static_cast<float>(event->angleDelta().y()) / wheel_delta_per_step;
    if (std::abs(wheel_steps) > wheel_step_epsilon) {
        camera_.distance *= std::pow(zoom_step_scale, wheel_steps);
        camera_.distance = std::clamp(camera_.distance, camera_.min_distance, camera_.max_distance);
    }
    event->accept();
}
