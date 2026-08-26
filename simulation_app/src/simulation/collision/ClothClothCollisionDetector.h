#pragma once

#include "gpu/scene/SimulationGpuView.h"
#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothClothCollisionDetector final
{
public:
    ClothClothCollisionDetector() = default;
    ClothClothCollisionDetector(const ClothClothCollisionDetector&) = delete;
    ClothClothCollisionDetector& operator=(const ClothClothCollisionDetector&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuView& views) const;
    void detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct CandidateDetectionProgram final
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

    struct DispatchSizeProgram final
    {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint local_size = -1;
    };

    void detect_pair(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionCandidateBuffers& collision_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    CandidateDetectionProgram candidate_detect_;
    DispatchSizeProgram dispatch_size_;
};
