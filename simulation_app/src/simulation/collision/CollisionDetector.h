#pragma once

#include "gpu/scene/SimulationGpuView.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class CollisionDetector final
{
public:
    CollisionDetector() = default;
    CollisionDetector(const CollisionDetector&) = delete;
    CollisionDetector& operator=(const CollisionDetector&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);

    bool can_detect(const SimulationGpuView& views) const;
    void detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_prefit(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct CandidateDetectionProgram final
    {
        GLuint program = 0;
        GLint item_count = -1;
        GLint max_candidates = -1;
    };

    struct DispatchSizeProgram final
    {
        GLuint program = 0;
        GLint max_candidates = -1;
    };

    struct ClothClothDetectionProgram final
    {
        GLuint program = 0;
        GLint upper_vertex_offset = -1;
        GLint upper_vertex_count = -1;
        GLint upper_bvh_root = -1;
        GLint lower_vertex_offset = -1;
        GLint lower_vertex_count = -1;
        GLint lower_bvh_root = -1;
        GLint max_candidates = -1;
    };

    void detect_cloth_vertex_body_face(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_edge_body_edge(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_face_body_vertex(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_cloth_vertex_face(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionCandidateBuffers& collision_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    bool can_detect_prefit(const SimulationGpuView& views) const;

    CandidateDetectionProgram cloth_vertex_body_face_;
    CandidateDetectionProgram cloth_edge_body_edge_;
    CandidateDetectionProgram cloth_face_body_vertex_;
    ClothClothDetectionProgram cloth_cloth_vertex_face_;
    DispatchSizeProgram dispatch_size_;
};
