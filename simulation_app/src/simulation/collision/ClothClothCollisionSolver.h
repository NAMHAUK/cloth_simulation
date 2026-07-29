#pragma once

#include "simulation/SimulationGpuViews.h"
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

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
                    float collision_thickness,
                    float collision_stiffness,
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
        GLint max_candidates = -1;
        GLint collision_thickness = -1;
        GLint collision_stiffness = -1;
        GLint body_triangle_count = -1;
        GLint upper_vertex_offset = -1;
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
        GLint max_candidates = -1;
        GLint collision_thickness = -1;
        GLint collision_stiffness = -1;
        GLint search_radius_squared = -1;
        GLint upper_vertex_offset = -1;
    } initial_accumulate_;
    BodyTriangleIdBuildStage body_triangle_id_build_;
    ApplyStage apply_;
    float collision_thickness_ = 0.0f;
    float collision_stiffness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float surface_search_radius_ = 0.0f;

    void apply_corrections(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
};
