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
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class ClothGpuState;
class SimulationPipeline final
{
public:
    explicit SimulationPipeline(SimulationParams params = default_simulation_params);
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void prefit_garments(SceneGpuState& gpu_state,
                         std::uint32_t iteration_count,
                         QOpenGLFunctions_4_5_Core& gl);
    void step(SceneState& scene,
              SceneGpuState& gpu_state,
              std::uint32_t motion_step_index,
              QOpenGLFunctions_4_5_Core& gl);
    bool is_initialized() const;

    // GPU timing
    void collect_timings(QOpenGLFunctions_4_5_Core& gl);
    void reset_timings();

private:
    // Simulation
    void update_character_motion(SceneState& scene,
                                 SceneGpuState& gpu_state,
                                 std::uint32_t motion_step_index,
                                 std::uint32_t substep,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    void integrate_cloth(const SceneState& scene,
                         const ClothGpuState& cloth_state,
                         QOpenGLFunctions_4_5_Core& gl) const;

    // GPU timing
    struct TimingBatch final
    {
        std::vector<GLuint> queries;
        std::uint32_t substep_count = 0;
        std::uint32_t iteration_count = 0;
        std::uint64_t motion_generation = 0;
        bool is_pending = false;
    };

    struct TimingTotals final
    {
        GLuint64 edge_detection_ns = 0;
        GLuint64 edge_correction_ns = 0;
        GLuint64 cloth_bounds_ns = 0;
        GLuint64 substep_ns = 0;
        std::uint64_t substep_count = 0;
    };

    TimingBatch& begin_timing_batch(QOpenGLFunctions_4_5_Core& gl);
    void print_timings() const;

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
    std::vector<TimingBatch> timing_batches_;
    TimingTotals timing_totals_;
    std::uint64_t motion_generation_ = 0;
    bool should_print_timings_ = false;
};
