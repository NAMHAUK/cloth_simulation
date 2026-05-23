#include "rendering/SimulationViewport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>

#include <QMouseEvent>
#include <QWheelEvent>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int playback_tick_ms = 16;

// 카메라 parameters
constexpr float default_camera_yaw = 0.75f * pi;
constexpr float default_camera_pitch = 15.0f * pi / 180.0f;
constexpr float default_camera_distance = 4.0f;

constexpr float character_camera_yaw = 0.5f * pi;
constexpr float character_camera_pitch = 10.0f * pi / 180.0f;

constexpr float character_camera_distance_min = 1.5f;
constexpr float character_camera_distance_scale = 2.2f;

constexpr float character_camera_near_min = 0.05f;
constexpr float character_camera_near_scale = 0.15f;

constexpr float character_camera_far_min = 10.0f;
constexpr float character_camera_far_scale = 12.0f;

// 카메라 control parameters
constexpr float orbit_sensitivity    = 0.006f;
constexpr float max_camera_pitch     = 85.0f * pi / 180.0f;
constexpr float pan_distance_scale   = 0.0015f;
constexpr float wheel_delta_per_step = 120.0f;
constexpr float wheel_step_epsilon   = 0.0001f;
constexpr float zoom_step_scale      = 0.88f;

// scene parameters
constexpr glm::vec3 background_color{0.07f, 0.09f, 0.12f};
constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

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

std::filesystem::path shader_path(const char* file_name)
{
    return std::filesystem::path(PROJECT_ROOT_DIR) / "simulation_app" / "shaders" / file_name;
}
}

SimulationViewport::SimulationViewport(QWidget* parent)
    : QOpenGLWidget(parent)
{
    camera_.yaw_radians   = default_camera_yaw;
    camera_.pitch_radians = default_camera_pitch;
    camera_.distance      = default_camera_distance;

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // tick마다 frame update
    frame_timer_.setInterval(playback_tick_ms);
    connect(&frame_timer_, &QTimer::timeout, this, [this]() {
        const double elapsed_seconds = static_cast<double>(playback_timer_.elapsed()) / 1000.0;
        if (runtime_.update_playback_frame(elapsed_seconds)) {
            update();
        }
    });
    frame_timer_.start();
}

SimulationViewport::~SimulationViewport()
{
    makeCurrent();
    runtime_.release_gpu(*this);
    doneCurrent();
}

void SimulationViewport::set_character_mesh(CharacterMesh mesh)
{
    if (gl_initialized_) {
        makeCurrent();
        runtime_.set_character_mesh(std::move(mesh), *this);
        doneCurrent();
    } else {
        runtime_.set_character_mesh(std::move(mesh));
    }

    playback_timer_.restart();
    reset_camera_to_character();

    update();
}

void SimulationViewport::add_garment_mesh(GarmentMesh mesh)
{
    if (gl_initialized_) {
        makeCurrent();
        runtime_.add_garment_mesh(std::move(mesh), *this);
        doneCurrent();
    } else {
        runtime_.add_garment_mesh(std::move(mesh));
    }

    update();
}

void SimulationViewport::initializeGL()
{
    initializeOpenGLFunctions();
    gl_initialized_ = true;

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    if (!runtime_.initialize_gpu(shader_path("viewer.vert"), shader_path("viewer.frag"), *this)) {
        return;
    }

    // render state 초기화
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

}

void SimulationViewport::resizeGL(int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void SimulationViewport::paintGL()
{
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!runtime_.is_gpu_initialized()) {
        return;
    }

    // MVP 계산 -> shader uMVP로 전달
    const glm::mat4 mvp = make_mvp(camera_, width(), height());
    runtime_.sync_gpu(*this);
    runtime_.draw(mvp, *this);
}

// Camera //
// 현재 모션의 캐릭터 bounding box 기준으로 카메라 초기화
void SimulationViewport::reset_camera_to_character()
{
    const SimulationScene& scene = runtime_.scene();
    if (!scene.has_character()) {
        return;
    }

    const CharacterMesh& character_mesh = scene.character_mesh();
    camera_.target = character_mesh.bounds_center;
    camera_.yaw_radians = character_camera_yaw;
    camera_.pitch_radians = character_camera_pitch;
    camera_.distance = std::max(character_camera_distance_min, character_mesh.bounds_radius * character_camera_distance_scale);
    camera_.min_distance = std::max(character_camera_near_min, character_mesh.bounds_radius * character_camera_near_scale);
    camera_.max_distance = std::max(character_camera_far_min, character_mesh.bounds_radius * character_camera_far_scale);
    camera_.has_last_mouse = false;
}

// Mouse Event //
void SimulationViewport::mousePressEvent(QMouseEvent* event)
{
    camera_.last_mouse_position = event->pos();
    camera_.has_last_mouse = true;
    event->accept();
}

void SimulationViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (!camera_.has_last_mouse) {
        event->ignore();
        return;
    }

    const QPoint delta = event->pos() - camera_.last_mouse_position;
    camera_.last_mouse_position = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        orbit_camera(camera_, delta);
    } else if ((event->buttons() & Qt::RightButton) || (event->buttons() & Qt::MiddleButton)) {
        pan_camera(camera_, delta, camera_position(camera_));
    } else {
        event->ignore();
        return;
    }

    update();
    event->accept();
}

void SimulationViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons() == Qt::NoButton) {
        camera_.has_last_mouse = false;
    }
    event->accept();
}

void SimulationViewport::wheelEvent(QWheelEvent* event)
{
    const float wheel_steps = static_cast<float>(event->angleDelta().y()) / wheel_delta_per_step;
    if (std::abs(wheel_steps) > wheel_step_epsilon) {
        camera_.distance *= std::pow(zoom_step_scale, wheel_steps);
        camera_.distance = std::clamp(camera_.distance, camera_.min_distance, camera_.max_distance);
        update();
    }
    event->accept();
}
