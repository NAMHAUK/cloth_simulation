#pragma once

#include "asset/GarmentAsset.h"
#include "asset/MotionAsset.h"
#include "gpu/scene/SceneGpuState.h"
#include "rendering/RenderPipeline.h"
#include "scene/SceneState.h"
#include "simulation/SimulationPipeline.h"

#include <functional>
#include <cstdint>

#include <glm/mat4x4.hpp>

#include <QTimer>

class QOpenGLFunctions_4_5_Core;
struct ShaderPaths;

class SimulationController final {
public:
    using GlContextTask = std::function<void(QOpenGLFunctions_4_5_Core&)>;

    struct ViewportCallbacks final {
        std::function<bool()> is_ready;
        std::function<void(GlContextTask)> run_with_gl_context;
        std::function<void()> request_update;
        std::function<void(const CharacterMesh&)> reset_camera_to_character;
    };

    SimulationController();
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

    void set_viewport_callbacks(ViewportCallbacks callbacks);

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh);
    void add_garment_mesh(GarmentMesh mesh);

    // GPU / rendering //
    bool initialize_gpu(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool is_gpu_initialized() const;
    void draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu();

private:
    void tick_frame();
    bool is_viewport_ready() const;

    // CPU-side scene state //
    SceneState scene_;

    // GPU-side dynamic simulation state //
    SceneGpuState gpu_state_;

    // Simulation pass orchestration //
    SimulationPipeline simulation_pipeline_;

    // Rendering orchestration //
    RenderPipeline render_pipeline_;

    std::uint64_t simulation_step_count_ = 0;
    std::uint64_t motion_step_count_ = 0;
    QTimer frame_timer_;

    ViewportCallbacks viewport_callbacks_;
    bool gpu_released_ = false;
};
