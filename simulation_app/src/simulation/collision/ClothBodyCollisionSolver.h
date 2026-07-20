#pragma once

#include "simulation/SimulationGpuViews.h"
#include "utils/GpuElapsedTimer.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
#define CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING 0
#endif

class ClothBodyCollisionSolver final {
public:
    ClothBodyCollisionSolver() = default;
    ClothBodyCollisionSolver(const ClothBodyCollisionSolver&) = delete;
    ClothBodyCollisionSolver& operator=(const ClothBodyCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& cloth_vertex_body_face_accumulate_shader_path,
                    const std::filesystem::path& cloth_edge_body_edge_accumulate_shader_path,
                    const std::filesystem::path& body_vertex_cloth_face_accumulate_shader_path,
                    const std::filesystem::path& apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    float static_friction,
                    float dynamic_friction,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuViews& views) const;
    void solve(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateStage final {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint thickness = -1;
    };

    struct ApplyStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
        GLint static_friction = -1;
        GLint dynamic_friction = -1;
    };

    void clear_correction_sums(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void vf_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void ee_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void bf_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void apply_combined_corrections(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;

    AccumulateStage vf_accumulate_;
    AccumulateStage ee_accumulate_;
    AccumulateStage bf_accumulate_;
    ApplyStage apply_;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    mutable GpuElapsedTimer vf_accumulate_timer_;
    mutable GpuElapsedTimer ee_accumulate_timer_;
    mutable GpuElapsedTimer bf_accumulate_timer_;
    mutable GpuElapsedTimer vf_apply_timer_;
#endif
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float static_friction_ = 0.0f;
    float dynamic_friction_ = 0.0f;
};
