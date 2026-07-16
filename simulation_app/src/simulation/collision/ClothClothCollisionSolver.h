#pragma once

#include "simulation/SimulationGpuViews.h"
#include "utils/GpuElapsedTimer.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
#define CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING 0
#endif

class ClothClothCollisionSolver final {
public:
    ClothClothCollisionSolver() = default;
    ClothClothCollisionSolver(const ClothClothCollisionSolver&) = delete;
    ClothClothCollisionSolver& operator=(const ClothClothCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& accumulate_shader_path,
                    const std::filesystem::path& initial_accumulate_shader_path,
                    const std::filesystem::path& body_triangle_id_build_shader_path,
                    const std::filesystem::path& apply_shader_path,
                    float collision_gap,
                    float barrier_stiffness,
                    float penetration_tolerance,
                    float max_correction_length,
                    float surface_search_radius,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuViews& views) const;
    bool can_solve_initial(const SimulationGpuViews& views) const;
    bool can_build_body_triangle_ids(const SimulationGpuViews& views) const;
    bool build_body_triangle_ids(const SimulationGpuViews& views,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    void solve(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void solve_initial(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateStage final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint collision_gap = -1;
        GLint barrier_stiffness = -1;
        GLint penetration_tolerance = -1;
        GLint character_triangle_count = -1;
    };

    struct BodyTriangleIdBuildStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint search_radius_squared = -1;
    };

    struct ApplyStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
    };

    AccumulateStage accumulate_;
    struct InitialAccumulateStage final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint collision_gap = -1;
        GLint barrier_stiffness = -1;
        GLint penetration_tolerance = -1;
        GLint search_radius_squared = -1;
    } initial_accumulate_;
    BodyTriangleIdBuildStage body_triangle_id_build_;
    ApplyStage apply_;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    mutable GpuElapsedTimer accumulate_timer_;
    mutable GpuElapsedTimer apply_timer_;
#endif
    float collision_gap_ = 0.0f;
    float barrier_stiffness_ = 0.0f;
    float penetration_tolerance_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float surface_search_radius_ = 0.0f;

    void apply_corrections(const SimulationGpuViews& views,
                           QOpenGLFunctions_4_5_Core& gl) const;
};
