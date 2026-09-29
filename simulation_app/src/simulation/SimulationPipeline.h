#pragma once

#include "simulation/ClothIntegrator.h"
#include "simulation/SimulationParams.h"
#include "simulation/collision/ClothBodyCollisionSolver.h"
#include "simulation/collision/ClothClothCollisionSolver.h"
#include "simulation/collision/CollisionDetector.h"
#include "simulation/collision/GarmentPrefitSolver.h"
#include "simulation/collision/GroundCollisionSolver.h"
#include "simulation/constraints/AttachmentConstraintSolver.h"
#include "simulation/constraints/BendingConstraintSolver.h"
#include "simulation/constraints/StretchConstraintSolver.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class SimulationPipeline final
{
public:
    SimulationPipeline(SceneState& scene,
                       SceneGpuState& gpu_state,
                       SimulationParams params = default_simulation_params);
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void prefit_garments(std::uint32_t iteration_count, QOpenGLFunctions_4_5_Core& gl);
    void step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl);
    bool is_initialized() const;

private:
    void update_character_motion(std::uint32_t motion_step_index,
                                 std::uint32_t substep,
                                 QOpenGLFunctions_4_5_Core& gl) const;

    SceneState& scene_;
    SceneGpuState& gpu_state_;
    SimulationParams params_;
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
