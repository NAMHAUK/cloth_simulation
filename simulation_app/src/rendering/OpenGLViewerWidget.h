#pragma once

#include "assets/GarmentAsset.h"
#include "assets/MotionAsset.h"
#include "character/CharacterGpuState.h"
#include "cloth/ClothGpuState.h"
#include "rendering/GridGpuState.h"
#include "rendering/ViewerShaderProgram.h"

#include <cstdint>
#include <filesystem>

#include <glm/mat4x4.hpp>
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

class OpenGLViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
public:
    explicit OpenGLViewerWidget(QWidget* parent = nullptr);
    ~OpenGLViewerWidget() override;

    bool load_motion_asset(const std::filesystem::path& motion_asset_path);
    bool load_garment_asset(const std::filesystem::path& garment_asset_path);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void draw_garment_mesh();

    void reset_camera_to_character();
    bool update_current_frame_index();

    CharacterMesh character_mesh_;
    GarmentMesh garment_mesh_;
    ViewerShaderProgram viewer_shader_;
    CharacterGpuState character_gpu_state_;
    GridGpuState grid_gpu_state_;
    ClothGpuState cloth_gpu_state_;

    OrbitCamera camera_;
    
    bool gl_initialized_ = false;
    bool character_loaded_ = false;
    bool garment_loaded_ = false;
    bool is_playing_ = false;

    std::uint32_t current_frame_ = 0;
    std::uint32_t uploaded_frame_ = UINT32_MAX;
    QElapsedTimer playback_timer_;
    QTimer frame_timer_;
};
