#pragma once

#include "assets/MotionAsset.h"

#include <cstdint>
#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QTimer>

struct MeshGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    bool initialized = false;
};

struct GridGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertex_count = 0;
    bool initialized = false;
};

struct OrbitCamera {
    glm::vec3 target{};
    float yaw_radians = 0.0f;
    float pitch_radians = 0.0f;
    float distance = 3.0f;
    float min_distance = 0.25f;
    float max_distance = 50.0f;
    QPoint last_mouse_position;
    bool has_last_mouse = false;
};

class OpenGLViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit OpenGLViewerWidget(QWidget* parent = nullptr);
    ~OpenGLViewerWidget() override;

    bool load_motion_asset(const std::filesystem::path& motion_asset_path);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    GLuint compile_shader(GLenum type, const char* source);
    GLuint create_program();

    void initialize_character_mesh_gpu();
    void upload_character_frame_to_gpu(std::uint32_t frame_index);
    void delete_character_mesh_gpu();

    void upload_grid_to_gpu();
    void delete_grid_gpu();

    void reset_camera_to_character();
    bool update_current_frame_index();

    CharacterMesh character_mesh_;
    MeshGpu character_mesh_gpu_;
    GridGpu grid_gpu_;

    OrbitCamera camera_;
    
    GLuint shader_program_ = 0;
    GLint mvp_location_ = -1;
    GLint solid_mode_location_ = -1;
    GLint solid_color_location_ = -1;
    bool gl_initialized_ = false;
    bool character_loaded_ = false;
    bool is_playing_ = false;

    std::uint32_t current_frame_ = 0;
    std::uint32_t uploaded_frame_ = UINT32_MAX;
    QElapsedTimer playback_timer_;
    QTimer frame_timer_;
};
