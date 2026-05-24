#pragma once

#include "assets/GarmentAsset.h"
#include "assets/MotionAsset.h"
#include "gpu/SimulationGpuState.h"
#include "simulation/SimulationScene.h"

#include <cstddef>
#include <filesystem>

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SimulationRuntime final {
public:
    SimulationRuntime() = default;
    SimulationRuntime(const SimulationRuntime&) = delete;
    SimulationRuntime& operator=(const SimulationRuntime&) = delete;

    const SimulationScene& scene() const;

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    GarmentId add_garment_mesh(GarmentMesh mesh, QOpenGLFunctions_4_5_Core& gl);

    // Playback / simulation // 
    bool update_playback_frame(double elapsed_seconds);
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
    SimulationScene scene_;

    // GPU-side dynamic/render state //
    SimulationGpuState gpu_state_;
};
