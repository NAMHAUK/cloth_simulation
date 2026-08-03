#pragma once

#include <cstdint>
#include <functional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QElapsedTimer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>

class QMouseEvent;
class QLabel;
class QPushButton;
class QTimer;
class QWheelEvent;
class AssetBrowserPanel;
class GarmentCardsPanel;
class GarmentColorPanel;
class PlacementPanel;
struct ProjectPaths;

struct OrbitCamera
{
    glm::vec3 target{};
    float yaw_radians = 0.0f;
    float pitch_radians = 0.0f;
    float distance = 3.0f;
    float min_distance = 0.25f;
    float max_distance = 50.0f;
    QPoint last_mouse_position;
    bool has_last_mouse = false;
};

class Viewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
public:
    using InitializeCallback = std::function<bool(QOpenGLFunctions_4_5_Core&)>;
    using SceneRenderCallback = std::function<void(const glm::mat4&, QOpenGLFunctions_4_5_Core&)>;

    explicit Viewport(const ProjectPaths& project_paths, QWidget* parent = nullptr);
    ~Viewport() override;

    // Child UI
    AssetBrowserPanel& asset_browser_panel();
    PlacementPanel& placement_panel();
    GarmentColorPanel& garment_color_panel();
    GarmentCardsPanel& garment_cards_panel();
    void set_play_pause_callback(std::function<void()> callback);
    void set_default_pose_callback(std::function<void()> callback);
    void set_reset_callback(std::function<void()> callback);
    void set_simulation_button_state(bool simulation_running, bool buttons_enabled);
    void set_motion_loading(bool is_loading);
    void update_layout();

    // Rendering callbacks
    void set_initialize_callback(InitializeCallback callback);
    void set_scene_render_callback(SceneRenderCallback callback);
    QOpenGLFunctions_4_5_Core& gl_functions();

    // Camera
    void reset_camera(const glm::vec3& root_position);
    void set_camera_target(const glm::vec3& root_position);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Child UI
    void setup_motion_loading_indicator();

    // Rendering
    void update_render_time();
    void draw_display_fps();

    AssetBrowserPanel* asset_browser_panel_ = nullptr;
    PlacementPanel* placement_panel_ = nullptr;
    GarmentColorPanel* garment_color_panel_ = nullptr;
    GarmentCardsPanel* garment_cards_panel_ = nullptr;
    QLabel* motion_loading_indicator_ = nullptr;
    QTimer* motion_loading_timer_ = nullptr;
    int motion_loading_step_ = 0;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* default_pose_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    std::function<void()> play_pause_callback_;
    std::function<void()> default_pose_callback_;
    std::function<void()> reset_callback_;

    InitializeCallback initialize_callback_;
    SceneRenderCallback scene_render_callback_;
    OrbitCamera camera_;
    QElapsedTimer render_fps_timer_;

    std::uint64_t render_frame_count_ = 0;
    double render_fps_ = 0.0;
    double frame_ms_ = 0.0;
};
