#pragma once

#include "assets/GarmentAsset.h"
#include "assets/MotionAsset.h"
#include "simulation/SimulationRuntime.h"

#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QElapsedTimer>
#include <QTimer>

class QOpenGLFunctions_4_5_Core;
class SimulationViewport;

class SimulationController final {
public:
    explicit SimulationController(SimulationViewport& viewport);
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

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

    SimulationViewport& viewport_;
    SimulationRuntime runtime_;

    QElapsedTimer playback_timer_;
    QTimer frame_timer_;

    bool gpu_released_ = false;
};
