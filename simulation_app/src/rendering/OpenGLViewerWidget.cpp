#include "rendering/OpenGLViewerWidget.h"

#include <algorithm>
#include <cmath>
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

constexpr glm::vec3 background_color{0.07f, 0.09f, 0.12f};
constexpr glm::vec3 grid_color{0.25f, 0.29f, 0.34f};
constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

constexpr float orbit_sensitivity = 0.006f;
constexpr float max_camera_pitch = 85.0f * pi / 180.0f;
constexpr float pan_distance_scale = 0.0015f;
constexpr float wheel_delta_per_step = 120.0f;
constexpr float wheel_step_epsilon = 0.0001f;
constexpr float zoom_step_scale = 0.88f;

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

OpenGLViewerWidget::OpenGLViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    camera_.yaw_radians   = default_camera_yaw;
    camera_.pitch_radians = default_camera_pitch;
    camera_.distance      = default_camera_distance;

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // tick留덈떎 frame update
    frame_timer_.setInterval(playback_tick_ms);
    connect(&frame_timer_, &QTimer::timeout, this, [this]() {
        if (update_current_frame_index()) {
            update();
        }
    });
    frame_timer_.start();
}

OpenGLViewerWidget::~OpenGLViewerWidget()
{
    makeCurrent();
    cloth_gpu_state_.release(*this);
    character_gpu_state_.release(*this);
    grid_gpu_state_.release(*this);
    viewer_shader_.release(*this);
    doneCurrent();
}

void OpenGLViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    gl_initialized_ = true;

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    if (!viewer_shader_.load(shader_path("viewer.vert"), shader_path("viewer.frag"), *this)) {
        std::cerr << "Failed to create viewer shader program.\n";
        return;
    }


    // render state ?ㅼ젙
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    grid_gpu_state_.upload(*this);
    if (character_loaded_) {
        character_gpu_state_.upload_mesh(character_mesh_, *this);
        character_gpu_state_.upload_frame(character_mesh_, 0, *this);
        uploaded_frame_ = 0;
    }
    if (garment_loaded_) {
        cloth_gpu_state_.upload(garment_mesh_, *this);
        cloth_gpu_state_.reset_positions(garment_mesh_, *this);
    }
}

void OpenGLViewerWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void OpenGLViewerWidget::paintGL()
{
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!viewer_shader_.initialized()) {
        return;
    }

    // ?꾩옱 移대찓???곹깭濡?MVP 怨꾩궛 -> shader uMVP???꾨떖
    const glm::mat4 mvp = make_mvp(camera_, width(), height());
    viewer_shader_.bind(*this);
    viewer_shader_.set_mvp(mvp, *this);

    // grid rendering
    if (grid_gpu_state_.initialized()) {
        viewer_shader_.set_solid_color(grid_color, *this);
        glDepthMask(GL_FALSE);
        grid_gpu_state_.draw(*this);
        glDepthMask(GL_TRUE);
    }

    // motion mesh rendering
    if (character_loaded_ && character_gpu_state_.initialized()) {
        if (uploaded_frame_ != current_frame_) {
            character_gpu_state_.upload_frame(character_mesh_, current_frame_, *this);
            uploaded_frame_ = current_frame_;
        }

        viewer_shader_.set_vertex_color_mode(*this);
        character_gpu_state_.draw(*this);
    }

    draw_garment_mesh();
}


// Motion Playback //
bool OpenGLViewerWidget::update_current_frame_index()
{
    if (!character_loaded_ || !is_playing_ || character_mesh_.frame_count == 0) {
        return false;
    }

    const double elapsed_seconds = static_cast<double>(playback_timer_.elapsed()) / 1000.0;
    const std::uint32_t next_frame = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(elapsed_seconds * character_mesh_.fps) % character_mesh_.frame_count
    );

    if (next_frame == current_frame_) {
        return false;
    }

    current_frame_ = next_frame;
    return true;
}


// ?꾩옱 ?좏깮??motion???꾩껜 frame 罹먮┃??mesh vertex data瑜?RAM??upload (character_mesh_)
bool OpenGLViewerWidget::load_motion_asset(const std::filesystem::path& motion_asset_path)
{
    CharacterMesh next_character_mesh;
    if (!load_character_mesh(motion_asset_path, next_character_mesh)) {
        return false;
    }

    character_mesh_ = std::move(next_character_mesh);
    character_loaded_ = true;
    is_playing_ = true;
    current_frame_ = 0;
    uploaded_frame_ = UINT32_MAX;
    playback_timer_.restart();
    reset_camera_to_character();

    if (gl_initialized_) {
        makeCurrent();
        character_gpu_state_.upload_mesh(character_mesh_, *this);
        character_gpu_state_.upload_frame(character_mesh_, 0, *this);
        uploaded_frame_ = 0;
        doneCurrent();
    }

    update();
    return true;
}

// Garment //
bool OpenGLViewerWidget::load_garment_asset(const std::filesystem::path& garment_asset_path)
{
    if (!load_garment_mesh(garment_asset_path, garment_mesh_)) {
        return false;
    }

    garment_loaded_ = true;

    if (gl_initialized_) {
        makeCurrent();
        cloth_gpu_state_.upload(garment_mesh_, *this);
        cloth_gpu_state_.reset_positions(garment_mesh_, *this);
        doneCurrent();
    }

    update();
    return true;
}

void OpenGLViewerWidget::draw_garment_mesh()
{
    if (!garment_loaded_ || !cloth_gpu_state_.initialized()) {
        return;
    }

    viewer_shader_.set_solid_color(garment_mesh_.color, *this);
    cloth_gpu_state_.draw(*this);
}

// Camera //
// ?좊땲硫붿씠??以묒떖?먯쑝濡?移대찓??reset
void OpenGLViewerWidget::reset_camera_to_character()
{
    camera_.target = character_mesh_.bounds_center;
    camera_.yaw_radians = character_camera_yaw;
    camera_.pitch_radians = character_camera_pitch;
    camera_.distance = std::max(character_camera_distance_min, character_mesh_.bounds_radius * character_camera_distance_scale);
    camera_.min_distance = std::max(character_camera_near_min, character_mesh_.bounds_radius * character_camera_near_scale);
    camera_.max_distance = std::max(character_camera_far_min, character_mesh_.bounds_radius * character_camera_far_scale);
    camera_.has_last_mouse = false;
}


// Mouse Event //
void OpenGLViewerWidget::mousePressEvent(QMouseEvent* event)
{
    camera_.last_mouse_position = event->pos();
    camera_.has_last_mouse = true;
    event->accept();
}

void OpenGLViewerWidget::mouseMoveEvent(QMouseEvent* event)
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

void OpenGLViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->buttons() == Qt::NoButton) {
        camera_.has_last_mouse = false;
    }
    event->accept();
}

void OpenGLViewerWidget::wheelEvent(QWheelEvent* event)
{
    const float wheel_steps = static_cast<float>(event->angleDelta().y()) / wheel_delta_per_step;
    if (std::abs(wheel_steps) > wheel_step_epsilon) {
        camera_.distance *= std::pow(zoom_step_scale, wheel_steps);
        camera_.distance = std::clamp(camera_.distance, camera_.min_distance, camera_.max_distance);
        update();
    }
    event->accept();
}
