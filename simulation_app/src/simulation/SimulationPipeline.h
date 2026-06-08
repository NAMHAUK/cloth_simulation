#pragma once

#include "simulation/forces/SimulationForceField.h"
#include "simulation/forces/ExternalForceSolver.h"
#include "simulation/constraints/StretchConstraintSolver.h"
#include "simulation/constraints/BendingConstraintSolver.h"
#include "simulation/collision/GroundCollisionSolver.h"
#include "simulation/collision/CharacterCollisionSolver.h"

#include <cstdint>

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
    bool step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_count, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_solve_constraint_iteration(const ClothPositionBufferView& position_view,
                                        const DistanceConstraintBufferView& stretch_constraint_view,
                                        const DistanceConstraintBufferView& bending_constraint_view,
                                        const CharacterTriangleGeometryResources& character_geometry,
                                        bool has_character) const;

    SimulationForceField force_field_;
    ExternalForceSolver external_force_solver_;
    StretchConstraintSolver stretch_constraint_solver_;
    BendingConstraintSolver bending_constraint_solver_;
    GroundCollisionSolver ground_collision_solver_;
    CharacterCollisionSolver character_collision_solver_;
    bool initialized_ = false;
};
