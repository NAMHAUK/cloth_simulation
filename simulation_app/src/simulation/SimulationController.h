#pragma once

#include "asset/GarmentAsset.h"
#include "asset/MotionAsset.h"
#include "gpu/scene/SceneGpuResources.h"
#include "rendering/SceneRenderer.h"
#include "scene/SceneState.h"
#include "simulation/ClothSimulationPipeline.h"

#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QElapsedTimer>
#include <QTimer>

class QOpenGLFunctions_4_5_Core;
class SceneViewport;

class SimulationController final {
public:
    explicit SimulationController(SceneViewport& viewport);
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh);
    void add_garment_mesh(GarmentMesh mesh);

    // Playback / simulation //
    void set_playing(bool playing);

    // GPU / rendering //
    bool initialize_gpu(const std::filesystem::path& vertex_shader_path,
                        const std::filesystem::path& fragment_shader_path,
                        QOpenGLFunctions_4_5_Core& gl);
    bool is_gpu_initialized() const;
    void draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu();

private:
    void tick_frame();
    bool simulation_step(QOpenGLFunctions_4_5_Core& gl);
    void sync_gpu(QOpenGLFunctions_4_5_Core& gl);

    SceneViewport& viewport_;

    // CPU-side scene state //
    SceneState scene_;

    // GPU-side dynamic simulation state //
    SceneGpuResources gpu_state_;

    // Future cloth solver / collision / constraint pass orchestration //
    ClothSimulationPipeline cloth_pipeline_;

    // Rendering orchestration //
    SceneRenderer renderer_;

    QElapsedTimer playback_timer_;
    QTimer frame_timer_;

    bool gpu_released_ = false;
};
