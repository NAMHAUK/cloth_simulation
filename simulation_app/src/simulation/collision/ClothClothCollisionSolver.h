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
    void solve(const SceneGpuState& gpu_state,
               QOpenGLFunctions_4_5_Core& gl,
               const GLuint* edge_timestamps = nullptr) const;
    void solve_initial(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateProgram final
    {
        GLuint program = 0;
        GLint max_candidates_loc = -1;
        GLint upper_vertex_offset_loc = -1;
        GLint upper_vertex_count_loc = -1;
    };

    AccumulateProgram accumulate_;
    AccumulateProgram initial_accumulate_;
    AccumulateProgram edge_accumulate_;
    GLuint apply_program_ = 0;
    GLint cloth_vertex_count_loc_ = -1;

    float collision_thickness_ = 0.0f;
    float collision_stiffness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float surface_search_radius_ = 0.0f;

    void solve(const SceneGpuState& gpu_state,
               const AccumulateProgram& shader,
               bool should_solve_edges,
               QOpenGLFunctions_4_5_Core& gl,
               const GLuint* edge_timestamps) const;
};
