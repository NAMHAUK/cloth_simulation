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
    struct AccumulateProgram final
    {
        GLuint program = 0;
        GLint max_candidates_loc = -1;
        GLint upper_vertex_offset_loc = -1;
    };

    struct BodyTriangleIndexBuildProgram final
    {
        GLuint program = 0;
        GLint vertex_count_loc = -1;
        GLint arm_triangle_ranges_loc = -1;
    };

    AccumulateProgram accumulate_;
    AccumulateProgram initial_accumulate_;
    BodyTriangleIndexBuildProgram body_triangle_index_build_;
    GLuint apply_program_ = 0;
    GLint body_triangle_count_loc_ = -1;
    GLint cloth_vertex_count_loc_ = -1;

    float collision_thickness_ = 0.0f;
    float collision_stiffness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float surface_search_radius_ = 0.0f;

    void solve(const SceneGpuState& gpu_state,
               const AccumulateProgram& shader,
               QOpenGLFunctions_4_5_Core& gl) const;
};
