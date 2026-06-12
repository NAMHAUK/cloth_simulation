#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/scene/SceneGpuState.h"
#include "rendering/RenderPipeline.h"
#include "scene/SceneState.h"
#include "simulation/SimulationPipeline.h"

#include <functional>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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
    void set_default_character_mesh(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    void add_garment_mesh(GarmentMesh mesh);
    void set_garment_placement(const glm::vec3& position_offset, float scale);

    // GPU / rendering //
    bool initialize_gpu(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool is_gpu_initialized() const;
    void start_simulation();
    void stop_simulation();
    bool is_simulation_running() const;
    bool is_default_pose() const;
    void draw(const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu();

private:
    void tick_frame();
    bool is_viewport_ready() const;
    void set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    bool has_pending_garment_placement() const;
    void apply_pending_garment_placement(QOpenGLFunctions_4_5_Core& gl);

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
    std::uint32_t editable_garment_id_ = 0;
    glm::vec3 pending_position_offset_{0.0f};
    float pending_scale_ = 1.0f;
    bool placement_position_changed_ = false;
    bool placement_scale_changed_ = false;
    bool simulation_running_ = false;
    bool is_default_pose_ = false;
    QTimer frame_timer_;

    ViewportCallbacks viewport_callbacks_;
    bool gpu_released_ = false;
};
