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
#include <optional>
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
                    CharacterMotion character_motion,
                    const std::vector<std::uint8_t>& triangle_part_labels,
                    QOpenGLFunctions_4_5_Core& gl);

    void start_simulation();
    void stop_simulation();
    void draw(const glm::mat4& mvp, float character_opacity, QOpenGLFunctions_4_5_Core& gl);

    void set_character_motion(CharacterMotion motion);
    void reset_scene();
    void return_to_default_pose();

    void set_garment_mesh(GarmentLayer layer, GarmentMesh mesh);
    void set_garment_placement(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void set_garment_color(GarmentLayer layer, const glm::vec3& color);
    void confirm_garment_placement();
    void discard_garment_placement(GarmentLayer layer);
    void cancel_placement_session();

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
    void load_default_character(CharacterMotion motion,
                                const std::vector<std::uint8_t>& triangle_part_labels);

    void tick_frame();
    void set_character_motion_state(CharacterMotion motion, QOpenGLFunctions_4_5_Core& gl);

    struct GarmentPlacementState final
    {
        glm::vec3 position_offset{0.0f};
        float scale = 1.0f;
    };

    std::array<glm::mat4, 2> make_placement_matrices() const;
    void reset_garment_placements();
    void release_gpu();
    void release_gpu(QOpenGLFunctions_4_5_Core& gl);

    SimulationParams params_;
    SceneState scene_;
    CharacterMotion default_character_motion_;
    SceneGpuState gpu_state_;
    SimulationPipeline simulation_pipeline_;
    RenderPipeline render_pipeline_;

    std::uint32_t motion_step_index_ = 0;
    std::array<std::optional<GarmentPlacementState>, 2> garment_placement_states_{};
    bool simulation_running_ = false;
    bool is_default_pose_ = false;
    QTimer frame_timer_;

    std::function<void(GlContextTask)> run_with_gl_context_;
};
