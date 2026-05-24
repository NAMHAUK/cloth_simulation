#pragma once

#include "assets/MotionAsset.h"

#include <glm/vec3.hpp>

#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>

class SimulationController;

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

class SimulationViewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
public:
    explicit SimulationViewport(QWidget* parent = nullptr);
    ~SimulationViewport() override;

    void set_controller(SimulationController* controller);
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
    SimulationController* controller_ = nullptr;
    OrbitCamera camera_;

    bool gl_initialized_ = false;
};
