#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <functional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QElapsedTimer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>

class QMouseEvent;
class QWheelEvent;

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

class SceneViewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
public:
    using InitializeCallback = std::function<bool(QOpenGLFunctions_4_5_Core&)>;
    using SceneRenderCallback = std::function<void(const glm::mat4&, QOpenGLFunctions_4_5_Core&)>;

    explicit SceneViewport(QWidget* parent = nullptr);
    ~SceneViewport() override;

    void set_initialize_callback(InitializeCallback callback);
    void set_scene_render_callback(SceneRenderCallback callback);
    bool is_gl_initialized() const;
    QOpenGLFunctions_4_5_Core& gl_functions();
    void reset_camera_to_character(const CharacterMesh& character_mesh);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void update_render_time();
    void draw_display_fps();

    InitializeCallback initialize_callback_;
    SceneRenderCallback scene_render_callback_;
    OrbitCamera camera_;
    QElapsedTimer render_fps_timer_;

    std::uint64_t render_frame_count_ = 0;
    double render_fps_ = 0.0;
    double frame_ms_ = 0.0;
    bool gl_initialized_ = false;
};
