#pragma once

#include <cstdint>
#include <functional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QElapsedTimer>
#include <QIcon>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLWidget>
#include <QPoint>

class QMouseEvent;
class QLabel;
class QPushButton;
class QScrollArea;
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
    bool is_dragging = false;
};

class Viewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

public:
    using InitializeCallback = std::function<void(QOpenGLFunctions_4_5_Core&)>;
    using SceneRenderCallback = std::function<void(const glm::mat4&, QOpenGLFunctions_4_5_Core&)>;

    explicit Viewport(const ProjectPaths& project_paths, QWidget* parent = nullptr);
    ~Viewport() override;

    void set_simulation_button_state(bool simulation_running, bool buttons_enabled);
    void set_loading_overlay_active(bool active);
    void update_layout();

    void reset_camera(const glm::vec3& root_position);
    void set_camera_target(const glm::vec3& root_position);

    AssetBrowserPanel& asset_browser_panel();
    PlacementPanel& placement_panel();
    GarmentColorPanel& garment_color_panel();
    GarmentCardsPanel& garment_cards_panel();
    QOpenGLFunctions_4_5_Core& gl_functions();

    void set_initialize_callback(InitializeCallback callback);
    void set_scene_render_callback(SceneRenderCallback callback);

Q_SIGNALS:
    void play_pause_requested();
    void default_pose_requested();
    void reset_requested();

protected:
    void initializeGL() override;

    void resizeGL(int width, int height) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void setup_panels(const ProjectPaths& project_paths);
    void setup_simulation_control_buttons();
    void setup_loading_overlay();

    void update_asset_browser_layout();
    void update_simulation_control_button_layout();
    void update_right_panel_layout();
    void update_loading_overlay_layout();

    glm::mat4 make_mvp() const;
    void update_frame_stats();
    void draw_frame_stats();

    AssetBrowserPanel* asset_browser_panel_ = nullptr;
    PlacementPanel* placement_panel_ = nullptr;
    GarmentColorPanel* garment_color_panel_ = nullptr;
    GarmentCardsPanel* garment_cards_panel_ = nullptr;
    QScrollArea* right_panel_ = nullptr;
    QLabel* loading_overlay_ = nullptr;
    QTimer* loading_spinner_timer_ = nullptr;
    int loading_spinner_step_ = 0;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* default_pose_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    QIcon play_icon_{QStringLiteral(":/icons/play.svg")};
    QIcon pause_icon_{QStringLiteral(":/icons/pause.svg")};
    InitializeCallback initialize_callback_;
    SceneRenderCallback scene_render_callback_;
    OrbitCamera camera_;
    QElapsedTimer render_fps_timer_;

    std::uint64_t render_frame_count_ = 0;
    double render_fps_ = 0.0;
    double frame_ms_ = 0.0;
};
