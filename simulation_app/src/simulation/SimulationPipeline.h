#pragma once

#include "gpu/scene/SimulationGpuView.h"
#include "simulation/ClothIntegrator.h"
#include "simulation/SimulationParams.h"
#include "simulation/collision/ClothBodyCollisionDetector.h"
#include "simulation/collision/ClothBodyCollisionSolver.h"
#include "simulation/collision/ClothClothCollisionDetector.h"
#include "simulation/collision/ClothClothCollisionSolver.h"
#include "simulation/collision/GarmentPrefitSolver.h"
#include "simulation/collision/GroundCollisionSolver.h"
#include "simulation/constraints/AttachmentConstraintSolver.h"
#include "simulation/constraints/BendingConstraintSolver.h"
#include "simulation/constraints/StretchConstraintSolver.h"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
struct GarmentObject;
class SimulationPipeline final
{
public:
    explicit SimulationPipeline(SimulationParams params = default_simulation_params);
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void prefit_garments(SceneGpuState& gpu_state,
                         const std::vector<const GarmentObject*>& garments,
                         QOpenGLFunctions_4_5_Core& gl);
    void step(SceneState& scene,
              SceneGpuState& gpu_state,
              std::uint32_t motion_step_index,
              QOpenGLFunctions_4_5_Core& gl);

private:
    void update_character_motion(SceneState& scene,
                                 SceneGpuState& gpu_state,
                                 std::uint32_t motion_step_index,
                                 std::uint32_t substep,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    void integrate_cloth(const SceneState& scene,
                         const SimulationGpuView& views,
                         QOpenGLFunctions_4_5_Core& gl) const;

public:
    bool is_initialized() const;

private:
    SimulationParams params_;
    ClothIntegrator cloth_integrator_;
    StretchConstraintSolver stretch_constraint_solver_;
    BendingConstraintSolver bending_constraint_solver_;
    AttachmentConstraintSolver attachment_constraint_solver_;
    GroundCollisionSolver ground_collision_solver_;
    ClothBodyCollisionDetector cloth_body_collision_detector_;
    ClothBodyCollisionSolver cloth_body_collision_solver_;
    ClothClothCollisionDetector cloth_cloth_collision_detector_;
    ClothClothCollisionSolver cloth_cloth_collision_solver_;
    GarmentPrefitSolver garment_prefit_solver_;
    float substep_dt_ = 0.0f;
    bool initialized_ = false;
};
