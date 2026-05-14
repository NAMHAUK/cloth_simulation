#include "rendering/OpenGLViewerWidget.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <QMouseEvent>
#include <QWheelEvent>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

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

std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open shader file: " << path << '\n';
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
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

    // tick마다 frame update
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
    delete_character_mesh_gpu();
    delete_grid_gpu();
    if (shader_program_ != 0) {
        glDeleteProgram(shader_program_);
    }
    doneCurrent();
}

void OpenGLViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    gl_initialized_ = true;

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';

    shader_program_ = create_program();
    if (shader_program_ == 0) {
        std::cerr << "Failed to create viewer shader program.\n";
        return;
    }

    // uniform들의 위치 저장
    mvp_location_         = glGetUniformLocation(shader_program_, "uMVP");
    solid_mode_location_  = glGetUniformLocation(shader_program_, "uUseSolidColor");
    solid_color_location_ = glGetUniformLocation(shader_program_, "uSolidColor");

    // render state 설정
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    upload_grid_to_gpu();
    if (character_loaded_) {
        initialize_character_mesh_gpu();
        upload_character_frame_to_gpu(0);
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

    if (shader_program_ == 0) {
        return;
    }

    // 현재 카메라 상태로 MVP 계산 -> shader uMVP에 전달
    const glm::mat4 mvp = make_mvp(camera_, width(), height());
    glUseProgram(shader_program_);
    glUniformMatrix4fv(mvp_location_, 1, GL_FALSE, glm::value_ptr(mvp));

    // grid rendering
    if (grid_gpu_.initialized) {
        glUniform1i(solid_mode_location_, 1);
        glUniform3f(solid_color_location_, grid_color.r, grid_color.g, grid_color.b);
        glDepthMask(GL_FALSE);
        glBindVertexArray(grid_gpu_.vao);
        glDrawArrays(GL_LINES, 0, grid_gpu_.vertex_count);
        glDepthMask(GL_TRUE);
    }

    // motion mesh rendering
    if (character_loaded_ && character_mesh_gpu_.initialized) {
        if (uploaded_frame_ != current_frame_) {
            upload_character_frame_to_gpu(current_frame_);
        }

        glUniform1i(solid_mode_location_, 0);
        glBindVertexArray(character_mesh_gpu_.vao);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(character_mesh_.index_count),
            GL_UNSIGNED_INT,
            nullptr
        );
    }
}


// Shader program setup//
GLuint OpenGLViewerWidget::compile_shader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile failed: " << log << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint OpenGLViewerWidget::create_program()
{
    const auto vertex_shader_source = read_text_file(shader_path("viewer.vert"));
    const auto fragment_shader_source = read_text_file(shader_path("viewer.frag"));
    if (!vertex_shader_source || !fragment_shader_source) {
        return 0;
    }

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source->c_str());
    if (vertex_shader == 0) {
        return 0;
    }

    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source->c_str());
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link failed: " << log << '\n';
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        glDeleteProgram(program);
        return 0;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
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


// 캐릭터 mesh upload //
// 캐릭터 mesh GPU buffer 초기화
void OpenGLViewerWidget::initialize_character_mesh_gpu()
{
    if (!character_mesh_gpu_.initialized) {
        glGenVertexArrays(1, &character_mesh_gpu_.vao);
        glGenBuffers(1, &character_mesh_gpu_.vbo);
        glGenBuffers(1, &character_mesh_gpu_.ebo);
        character_mesh_gpu_.initialized = true;
    }

    glBindVertexArray(character_mesh_gpu_.vao);

    glBindBuffer(GL_ARRAY_BUFFER, character_mesh_gpu_.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(character_mesh_.vertex_count * 3 * sizeof(float)),
        character_mesh_.vertices.data(),
        GL_DYNAMIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, character_mesh_gpu_.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(character_mesh_.indices.size() * sizeof(std::uint32_t)),
        character_mesh_.indices.data(),
        GL_STATIC_DRAW
    );
}

// 현재 선택된 motion의 전체 frame 캐릭터 mesh vertex data를 RAM에 upload (character_mesh_)
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
        initialize_character_mesh_gpu();
        upload_character_frame_to_gpu(0);
        doneCurrent();
    }

    update();
    return true;
}

// RAM에 있던 character_mesh_중 frame index에 해당하는 mesh vertex data를 GPU에 upload
void OpenGLViewerWidget::upload_character_frame_to_gpu(std::uint32_t frame_index)
{
    const std::size_t frame_offset =
        static_cast<std::size_t>(frame_index) *
        static_cast<std::size_t>(character_mesh_.vertex_count) * 3;

    glBindBuffer(GL_ARRAY_BUFFER, character_mesh_gpu_.vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(character_mesh_.vertex_count * 3 * sizeof(float)),
        character_mesh_.vertices.data() + frame_offset
    );
    uploaded_frame_ = frame_index;
}

void OpenGLViewerWidget::delete_character_mesh_gpu()
{
    if (character_mesh_gpu_.ebo != 0) {
        glDeleteBuffers(1, &character_mesh_gpu_.ebo);
    }
    if (character_mesh_gpu_.vbo != 0) {
        glDeleteBuffers(1, &character_mesh_gpu_.vbo);
    }
    if (character_mesh_gpu_.vao != 0) {
        glDeleteVertexArrays(1, &character_mesh_gpu_.vao);
    }
    character_mesh_gpu_ = {};
}


// Grid //
void OpenGLViewerWidget::upload_grid_to_gpu()
{
    if (grid_gpu_.initialized) {
        return;
    }

    constexpr int half_line_count = 10;
    constexpr float spacing = 0.5f;
    constexpr float half_size = static_cast<float>(half_line_count) * spacing;

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>((half_line_count * 2 + 1) * 4 * 3));
    for (int line = -half_line_count; line <= half_line_count; ++line) {
        const float offset = static_cast<float>(line) * spacing;

        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(-half_size);
        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(half_size);

        vertices.push_back(-half_size);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
        vertices.push_back(half_size);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
    }

    glGenVertexArrays(1, &grid_gpu_.vao);
    glGenBuffers(1, &grid_gpu_.vbo);
    grid_gpu_.vertex_count = static_cast<GLsizei>(vertices.size() / 3);
    grid_gpu_.initialized = true;

    glBindVertexArray(grid_gpu_.vao);
    glBindBuffer(GL_ARRAY_BUFFER, grid_gpu_.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
}

void OpenGLViewerWidget::delete_grid_gpu()
{
    if (grid_gpu_.vbo != 0) {
        glDeleteBuffers(1, &grid_gpu_.vbo);
    }
    if (grid_gpu_.vao != 0) {
        glDeleteVertexArrays(1, &grid_gpu_.vao);
    }
    grid_gpu_ = {};
}


// Camera //
// 애니메이션 중심점으로 카메라 reset
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
