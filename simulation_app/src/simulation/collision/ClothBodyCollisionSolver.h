#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct BodyCollisionParams;
class SceneGpuState;

class ClothBodyCollisionSolver final
{
public:
    explicit ClothBodyCollisionSolver(const BodyCollisionParams& params);
    ClothBodyCollisionSolver(const ClothBodyCollisionSolver&) = delete;
    ClothBodyCollisionSolver& operator=(const ClothBodyCollisionSolver&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void solve(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateProgram final
    {
        GLuint program = 0;
        GLint max_candidates_loc = -1;
    };

    void clear_correction_sums(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void accumulate_cloth_vertex_body_face(const SceneGpuState& gpu_state,
                                           QOpenGLFunctions_4_5_Core& gl) const;
    void accumulate_cloth_edge_body_edge(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void apply_combined_corrections(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;

    AccumulateProgram cloth_vertex_body_face_;
    AccumulateProgram cloth_edge_body_edge_;
    GLuint apply_program_ = 0;
    GLint cloth_vertex_count_loc_ = -1;

    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float static_friction_ = 0.0f;
    float dynamic_friction_ = 0.0f;
};
