#pragma once

#include "gpu/scene/PbdBufferBindings.h"
#include "gpu/scene/PbdGpuState.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationParams.h"
#include "simulation/pbd/ClothIntegrator.h"
#include "simulation/pbd/collision/ClothBodyCollisionSolver.h"
#include "simulation/pbd/collision/ClothClothCollisionSolver.h"
#include "simulation/pbd/collision/CollisionDetector.h"
#include "simulation/pbd/collision/GarmentPrefitSolver.h"
#include "simulation/pbd/collision/GroundCollisionSolver.h"
#include "simulation/pbd/constraints/AttachmentConstraintSolver.h"
#include "simulation/pbd/constraints/BendingConstraintSolver.h"
#include "simulation/pbd/constraints/StretchConstraintSolver.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class PbdSimulation final : public Simulation
{
public:
    PbdSimulation(SceneState& scene, SceneGpuState& gpu_state, SimulationParams params = default_simulation_params);
    PbdSimulation(const PbdSimulation&) = delete;
    PbdSimulation& operator=(const PbdSimulation&) = delete;

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
    void release_collision_buffers(QOpenGLFunctions_4_5_Core& gl);

    SceneState& scene_;
    SceneGpuState& gpu_state_;
    SimulationParams params_;
    PbdGpuState pbd_gpu_state_;
    PbdBufferBindings buffer_bindings_;
    ClothIntegrator cloth_integrator_;
    StretchConstraintSolver stretch_constraint_solver_;
    BendingConstraintSolver bending_constraint_solver_;
    AttachmentConstraintSolver attachment_constraint_solver_;
    GroundCollisionSolver ground_collision_solver_;
    CollisionDetector collision_detector_;
    ClothBodyCollisionSolver cloth_body_collision_solver_;
    ClothClothCollisionSolver cloth_cloth_collision_solver_;
    GarmentPrefitSolver garment_prefit_solver_;
    float substep_dt_ = 0.0f;
    bool initialized_ = false;
};
