#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ClothCollisionParams;
class SceneGpuState;

class ClothClothCollisionSolver final
{
public:
    explicit ClothClothCollisionSolver(const ClothCollisionParams& params);
    ClothClothCollisionSolver(const ClothClothCollisionSolver&) = delete;
    ClothClothCollisionSolver& operator=(const ClothClothCollisionSolver&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void update_body_surface_mapping(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void solve(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void solve_initial(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateStage final
    {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint collision_thickness = -1;
        GLint collision_stiffness = -1;
        GLint body_triangle_count = -1;
        GLint upper_vertex_offset = -1;
    };

    struct BodyTriangleIndexBuildStage final
    {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint search_radius_squared = -1;
        GLint arm_triangle_ranges = -1;
    };

    struct ApplyStage final
    {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
    };

    AccumulateStage accumulate_;
    struct InitialAccumulateStage final
    {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint collision_thickness = -1;
        GLint collision_stiffness = -1;
        GLint search_radius_squared = -1;
        GLint upper_vertex_offset = -1;
    } initial_accumulate_;
    BodyTriangleIndexBuildStage body_triangle_index_build_;
    ApplyStage apply_;
    float collision_thickness_ = 0.0f;
    float collision_stiffness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float surface_search_radius_ = 0.0f;

    void apply_corrections(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
};
