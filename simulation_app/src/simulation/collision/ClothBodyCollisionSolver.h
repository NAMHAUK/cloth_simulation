#pragma once

#include "gpu/scene/SimulationGpuView.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct BodyCollisionParams;

class ClothBodyCollisionSolver final
{
public:
    explicit ClothBodyCollisionSolver(const BodyCollisionParams& params);
    ClothBodyCollisionSolver(const ClothBodyCollisionSolver&) = delete;
    ClothBodyCollisionSolver& operator=(const ClothBodyCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& cloth_vertex_body_face_accumulate_shader_path,
                    const std::filesystem::path& cloth_edge_body_edge_accumulate_shader_path,
                    const std::filesystem::path& body_vertex_cloth_face_accumulate_shader_path,
                    const std::filesystem::path& apply_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuView& views) const;
    void solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateStage final
    {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint thickness = -1;
    };

    struct ApplyStage final
    {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
        GLint static_friction = -1;
        GLint dynamic_friction = -1;
    };

    void clear_correction_sums(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void vf_accumulate(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void ee_accumulate(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void bf_accumulate(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void apply_combined_corrections(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;

    AccumulateStage vf_accumulate_;
    AccumulateStage ee_accumulate_;
    AccumulateStage bf_accumulate_;
    ApplyStage apply_;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float static_friction_ = 0.0f;
    float dynamic_friction_ = 0.0f;
};
