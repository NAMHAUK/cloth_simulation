#pragma once

#include "gpu/scene/PdGpuState.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationParams.h"
#include "simulation/pd/ProjectiveDynamicsSolver.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class PdSimulation final : public Simulation
{
public:
    PdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params = default_simulation_params);
    PdSimulation(const PdSimulation&) = delete;
    PdSimulation& operator=(const PdSimulation&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl) override;

    void step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl) override;
    void prepare_garments(const std::vector<const GarmentObject*>& placement_garments,
                          QOpenGLFunctions_4_5_Core& gl) override;

    void rebuild_garment_resources(GarmentLayer changed_layer, QOpenGLFunctions_4_5_Core& gl) override;
    void create_simulation_buffers(QOpenGLFunctions_4_5_Core& gl) override;

    void set_character_motion(QOpenGLFunctions_4_5_Core& gl) override;
    void update_character_pose(float frame_alpha, QOpenGLFunctions_4_5_Core& gl) override;

    void capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) override;
    void restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) override;
    void clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) override;

    bool is_initialized() const override;

    void release(QOpenGLFunctions_4_5_Core& gl) override;
    void release_garment_resources(QOpenGLFunctions_4_5_Core& gl) override;

private:
    void update_character_motion(std::uint32_t motion_step_index, std::uint32_t substep, QOpenGLFunctions_4_5_Core& gl);

    SceneState& scene_;
    SceneGpuState& gpu_state_;
    SimulationParams params_;
    PdGpuState pd_gpu_state_;
    ProjectiveDynamicsSolver projective_dynamics_solver_;
    float substep_dt_ = 0.0f;
    bool initialized_ = false;
};
