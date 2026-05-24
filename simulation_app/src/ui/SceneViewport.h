#pragma once

#include "io/MotionAsset.h"

#include <glm/vec3.hpp>

#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>

class AppController;
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
    explicit SceneViewport(QWidget* parent = nullptr);
    ~SceneViewport() override;

    void set_controller(AppController* controller);
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
    AppController* controller_ = nullptr;
    OrbitCamera camera_;

    bool gl_initialized_ = false;
};
