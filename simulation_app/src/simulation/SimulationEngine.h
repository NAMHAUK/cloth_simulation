#pragma once

#include "io/GarmentAsset.h"
#include "io/MotionAsset.h"
#include "gpu/scene/SceneGpuResources.h"
#include "rendering/SceneRenderer.h"
#include "scene/SceneState.h"

#include <cstddef>
#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SimulationEngine final {
public:
    SimulationEngine() = default;
    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;

    const SceneState& scene() const;

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    GarmentId add_garment_mesh(GarmentMesh mesh, QOpenGLFunctions_4_5_Core& gl);

    // Playback / simulation // 
    bool update_playback_frame(double playback_seconds);
    bool simulation_step(QOpenGLFunctions_4_5_Core& gl);
    void set_playing(bool playing);

    // GPU / rendering //
    bool initialize_gpu(const std::filesystem::path& vertex_shader_path,
                        const std::filesystem::path& fragment_shader_path,
                        QOpenGLFunctions_4_5_Core& gl);
    bool is_gpu_initialized() const;
    void sync_gpu(QOpenGLFunctions_4_5_Core& gl);
    void draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu(QOpenGLFunctions_4_5_Core& gl);

private:
    // CPU-side scene state //
    SceneState scene_;

    // GPU-side dynamic simulation state //
    SceneGpuResources gpu_state_;

    // Rendering orchestration //
    SceneRenderer renderer_;
};
