#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/scene/SceneGpuState.h"
#include "rendering/RenderPipeline.h"
#include "scene/SceneState.h"
#include "simulation/SimulationPipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QTimer>

class QOpenGLFunctions_4_5_Core;
struct ShaderPaths;

class SimulationController final
{
public:
    using GlContextTask = std::function<void(QOpenGLFunctions_4_5_Core&)>;

    struct ViewportCallbacks final
    {
        std::function<bool()> is_ready;
        std::function<void(GlContextTask)> run_with_gl_context;
        std::function<void()> request_update;
        std::function<void(const glm::vec3&)> reset_camera_to_character_root;
        std::function<void(const glm::vec3&)> set_camera_target;
    };

    SimulationController();
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

    // Initialization //
    bool initialize(const ShaderPaths& shader_paths,
                    CharacterMesh character_mesh,
                    const std::vector<std::uint8_t>& triangle_part_labels,
                    QOpenGLFunctions_4_5_Core& gl);
    void set_viewport_callbacks(ViewportCallbacks callbacks);

    // Scene editing //
    void set_character_mesh(CharacterMesh mesh);
    bool set_garment_mesh(GarmentLayer layer, GarmentMesh mesh);
    bool remove_garment_placement(GarmentLayer layer);
    void set_garment_placement(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void set_garment_color(GarmentLayer layer, const glm::vec3& color);
    bool confirm_garment_placement();
    void cancel_garment_placement();
    void reset_scene_to_default();
    void return_to_default_pose();

    // GPU / rendering //
    bool is_gpu_initialized() const;
    void start_simulation();
    void stop_simulation();
    bool is_simulation_running() const;
    bool is_default_pose() const;
    std::size_t garment_count() const;
    bool can_start_garment_placement() const;
    glm::vec3 garment_placement_color(GarmentLayer layer) const;
    void draw(const glm::mat4& mvp, float character_opacity, QOpenGLFunctions_4_5_Core& gl);
    void release_gpu();

private:
    // Initialization //
    bool initialize_gpu(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool load_default_character(CharacterMesh mesh,
                                const std::vector<std::uint8_t>& triangle_part_labels,
                                QOpenGLFunctions_4_5_Core& gl);

    struct GarmentPlacementState final
    {
        glm::vec3 position_offset{0.0f};
        float scale = 1.0f;
        bool is_active = false;
        bool position_changed = false;
        bool scale_changed = false;

        void clear()
        {
            position_offset = glm::vec3{0.0f};
            scale = 1.0f;
            is_active = false;
            clear_update();
        }

        bool has_update() const { return is_active && (position_changed || scale_changed); }

        void clear_update()
        {
            position_changed = false;
            scale_changed = false;
        }
    };

    void tick_frame();
    bool is_viewport_ready() const;
    void set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    bool has_garment_placement_update() const;
    void set_current_garment_placement(QOpenGLFunctions_4_5_Core& gl);
    bool build_garment_triangle_bvh(GarmentLayer layer);
    std::vector<GarmentLayer> garment_placement_layers() const;
    void restore_garment_placements(const std::vector<GarmentLayer>& layers, QOpenGLFunctions_4_5_Core& gl);
    void clear_garment_placements();

    // CPU-side scene state //
    SceneState scene_;
    CharacterMesh default_character_mesh_;

    // GPU-side dynamic simulation state //
    SceneGpuState gpu_state_;

    // Simulation pass orchestration //
    SimulationPipeline simulation_pipeline_;

    // Rendering orchestration //
    RenderPipeline render_pipeline_;

    std::uint64_t motion_step_index_ = 0;
    std::array<GarmentPlacementState, 2> garment_placements_{};
    bool simulation_running_ = false;
    bool is_default_pose_ = false;
    bool has_base_positions_ = false;
    QTimer frame_timer_;

    ViewportCallbacks viewport_callbacks_;
};
