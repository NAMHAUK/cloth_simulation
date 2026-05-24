#pragma once

#include "io/GarmentAsset.h"
#include "io/MotionAsset.h"
#include "simulation/SimulationEngine.h"

#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QElapsedTimer>
#include <QTimer>

class QOpenGLFunctions_4_5_Core;
class SceneViewport;

class AppController final {
public:
    explicit AppController(SceneViewport& viewport);
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh);
    void add_garment_mesh(GarmentMesh mesh);

    // GPU / rendering //
    bool initialize_gpu(const std::filesystem::path& vertex_shader_path,
                        const std::filesystem::path& fragment_shader_path,
                        QOpenGLFunctions_4_5_Core& gl);
    bool is_gpu_initialized() const;
    void draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu();

private:
    void tick_frame();

    SceneViewport& viewport_;
    SimulationEngine runtime_;

    QElapsedTimer playback_timer_;
    QTimer frame_timer_;

    bool gpu_released_ = false;
};
