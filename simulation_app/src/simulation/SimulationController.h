#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/scene/SceneGpuState.h"
#include "rendering/RenderPipeline.h"
#include "scene/SceneState.h"
#include "simulation/SimulationPipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <QObject>
#include <QTimer>

class QOpenGLFunctions_4_5_Core;
class SimulationController final : public QObject
{
    Q_OBJECT

public:
    using GlContextTask = std::function<void(QOpenGLFunctions_4_5_Core&)>;

    explicit SimulationController(SimulationParams params = default_simulation_params);
    ~SimulationController();

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;

    void set_run_with_gl_context(std::function<void(GlContextTask)> run_with_gl_context);
    void initialize(const std::filesystem::path& shader_dir,
                    CharacterMesh character_mesh,
                    const std::vector<std::uint8_t>& triangle_part_labels,
                    QOpenGLFunctions_4_5_Core& gl);

    void start_simulation();
    void stop_simulation();
    void draw(const glm::mat4& mvp, float character_opacity, QOpenGLFunctions_4_5_Core& gl);

    void set_character_mesh(CharacterMesh mesh);
    void reset_scene_to_default();
    void return_to_default_pose();

    bool set_garment_mesh(GarmentLayer layer, GarmentMesh mesh);
    bool remove_garment_placement(GarmentLayer layer);
    void set_garment_placement(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void apply_garment_placement_changes(QOpenGLFunctions_4_5_Core& gl);
    void set_garment_color(GarmentLayer layer, const glm::vec3& color);
    bool confirm_garment_placement();
    void cancel_garment_placement();

    bool is_gpu_initialized() const;
    bool is_simulation_running() const;
    std::size_t garment_count() const;
    bool can_start_garment_placement() const;

Q_SIGNALS:
    void viewport_update_requested();
    void camera_reset_requested(const glm::vec3& root_position);
    void camera_target_changed(const glm::vec3& root_position);

private:
    void initialize_gpu(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void load_default_character(CharacterMesh mesh,
                                const std::vector<std::uint8_t>& triangle_part_labels,
                                QOpenGLFunctions_4_5_Core& gl);

    void tick_frame();
    void set_character_mesh_state(CharacterMesh mesh, QOpenGLFunctions_4_5_Core& gl);

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

    bool add_garment(GarmentLayer layer, GarmentMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    bool replace_garment(GarmentLayer layer, GarmentMesh mesh, QOpenGLFunctions_4_5_Core& gl);
    bool build_garment_triangle_bvh(GarmentLayer layer);
    void restore_garment_placements(const std::vector<GarmentLayer>& layers, QOpenGLFunctions_4_5_Core& gl);
    void clear_garment_placements();
    void release_gpu();
    void release_gpu(QOpenGLFunctions_4_5_Core& gl);

    SimulationParams params_;
    SceneState scene_;
    CharacterMesh default_character_mesh_;
    SceneGpuState gpu_state_;
    SimulationPipeline simulation_pipeline_;
    RenderPipeline render_pipeline_;

    std::uint64_t motion_step_index_ = 0;
    std::array<GarmentPlacementState, 2> garment_placements_{};
    bool simulation_running_ = false;
    bool is_default_pose_ = false;
    QTimer frame_timer_;

    std::function<void(GlContextTask)> run_with_gl_context_;
};
