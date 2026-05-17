#pragma once

#include "assets/GarmentAsset.h"
#include "assets/MotionAsset.h"
#include "gpu/SimulationGpuState.h"
#include "simulation/SimulationScene.h"

#include <glm/vec3.hpp>

#include <QElapsedTimer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QTimer>

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

    void set_character_mesh(CharacterMesh mesh);
    void add_garment_mesh(GarmentMesh mesh);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void reset_camera_to_character();

    SimulationScene scene_;
    SimulationGpuState gpu_state_;
    OrbitCamera camera_;

    bool gl_initialized_ = false;

    QElapsedTimer playback_timer_;
    QTimer frame_timer_;
};
