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

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
class SceneState;
class ClothGpuState;
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

    // Motion timing
    void start_motion_timing(const std::filesystem::path& motion_path);
    void finish_motion_timing();
    void collect_motion_timing(QOpenGLFunctions_4_5_Core& gl, bool wait_for_results = false);

private:
    void update_character_motion(SceneState& scene,
                                 SceneGpuState& gpu_state,
                                 std::uint32_t motion_step_index,
                                 std::uint32_t substep,
                                 QOpenGLFunctions_4_5_Core& gl,
                                 const GLuint* body_bounds_queries) const;
    void integrate_cloth(const SceneState& scene,
                         const ClothGpuState& cloth_state,
                         QOpenGLFunctions_4_5_Core& gl) const;

public:
    bool is_initialized() const;

private:
    // Motion timing
    enum TimingPoint : std::size_t
    {
        SubstepBegin,
        BodyBoundsBegin,
        BodyBoundsEnd,
        ClothBoundsBegin,
        ClothBoundsEnd,
        DetectBegin,
        DetectEnd,
        SubstepEnd,
        TimingPointCount,
    };

    struct MotionTiming final
    {
        std::string name;
        std::uint32_t first_frame = 0;
        std::uint32_t last_frame = 0;
        std::uint64_t substeps = 0;
        std::uint64_t skipped_substeps = 0;
        std::size_t pending_samples = 0;
        std::array<double, 6> gpu_ns{};
        bool is_finished = false;
    };

    struct TimingSample final
    {
        std::array<GLuint, TimingPointCount> queries{};
        std::vector<std::array<GLuint, 2>> distance_queries;
        std::vector<std::array<GLuint, 2>> collision_queries;
        MotionTiming* motion = nullptr;
    };

    TimingSample* begin_timing_sample(std::uint32_t frame_index);
    static void record_timestamp(const TimingSample* sample,
                                 TimingPoint point,
                                 QOpenGLFunctions_4_5_Core& gl);
    void print_finished_motion_timings();

    std::array<TimingSample, 64> timing_samples_{};
    std::size_t timing_read_index_ = 0;
    std::size_t pending_timing_samples_ = 0;
    std::deque<MotionTiming> motion_timings_;

    // Prefit timing
    void collect_prefit_timings(QOpenGLFunctions_4_5_Core& gl, bool wait_for_results);

    std::deque<std::array<GLuint, 2>> prefit_queries_;

    // Simulation
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
