#pragma once

#include "gpu/bvh/ClothBvhBoundsUpdater.h"
#include "simulation/SimulationGpuViews.h"
#include "simulation/SimulationSettings.h"
#include "simulation/forces/SimulationForceField.h"
#include "simulation/forces/ExternalForceSolver.h"
#include "simulation/constraints/StretchConstraintSolver.h"
#include "simulation/constraints/BendingConstraintSolver.h"
#include "simulation/constraints/AttachmentConstraintSolver.h"
#include "simulation/collision/GroundCollisionSolver.h"
#include "simulation/collision/ClothBodyCollisionDetector.h"
#include "simulation/collision/ClothBodyCollisionSolver.h"
#include "simulation/collision/ClothClothCollisionDetector.h"
#include "simulation/collision/ClothClothCollisionSolver.h"
#include "simulation/collision/GarmentPrefitSolver.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
struct ShaderPaths;

class SimulationPipeline final {
public:
    SimulationPipeline() = default;
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    bool is_initialized() const;
    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool prefit_garments(const SceneState& scene,
                         SceneGpuState& gpu_state,
                         const std::vector<std::uint32_t>& garment_ids,
                         QOpenGLFunctions_4_5_Core& gl);
    bool step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_index, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    static SimulationGpuViews collect_gpu_views(const SceneGpuState& gpu_state);
    bool update_cloth_bvh_bounds(const SimulationGpuViews& views,
                                 float bounds_margin,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    bool can_solve_constraint_iteration(const SimulationGpuViews& views) const;

    ClothBvhBoundsUpdater cloth_bvh_bounds_updater_;
    SimulationForceField force_field_;
    ExternalForceSolver external_force_solver_;
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
