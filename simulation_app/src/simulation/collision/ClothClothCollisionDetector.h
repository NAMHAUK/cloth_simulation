#pragma once

#include "gpu/bvh/ClothBvhBoundsUpdater.h"
#include "gpu/scene/SimulationGpuView.h"
#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ClothCollisionParams;

class ClothClothCollisionDetector final
{
public:
    explicit ClothClothCollisionDetector(const ClothCollisionParams& params);
    ClothClothCollisionDetector(const ClothClothCollisionDetector&) = delete;
    ClothClothCollisionDetector& operator=(const ClothClothCollisionDetector&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuView& views) const;
    void detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_initial(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
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

    void detect(const SimulationGpuView& views, float bounds_margin, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_pair(const ElementRange& upper_vertex_range,
                     const GarmentBvhRanges& upper_bvh,
                     const ElementRange& lower_vertex_range,
                     const GarmentBvhRanges& lower_bvh,
                     const CollisionCandidateBuffer& collision_candidates,
                     QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    ClothBvhBoundsUpdater bounds_updater_;
    CandidateDetectionProgram candidate_detect_;
    DispatchSizeProgram dispatch_size_;
    float initial_detection_distance_ = 0.0f;
    float detection_distance_ = 0.0f;
};
