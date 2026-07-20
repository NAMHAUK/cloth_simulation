#pragma once

#include "simulation/SimulationGpuViews.h"
#include "utils/GpuElapsedTimer.h"
#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_GPU_TIMING
#define CLOTH_SIM_COLLISION_GPU_TIMING 0
#endif

class ClothClothCollisionDetector final {
public:
    ClothClothCollisionDetector() = default;
    ClothClothCollisionDetector(const ClothClothCollisionDetector&) = delete;
    ClothClothCollisionDetector& operator=(const ClothClothCollisionDetector&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& candidate_detect_shader_path,
                    const std::filesystem::path& dispatch_size_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuViews& views) const;
    void detect(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct CandidateDetectionProgram final {
        GLuint program = 0;
        GLint upper_vertex_offset = -1;
        GLint upper_vertex_count = -1;
        GLint upper_triangle_offset = -1;
        GLint upper_bvh_node_offset = -1;
        GLint lower_vertex_offset = -1;
        GLint lower_vertex_count = -1;
        GLint lower_triangle_offset = -1;
        GLint lower_bvh_node_offset = -1;
        GLint max_candidates = -1;
    };

    struct DispatchSizeProgram final {
        GLuint program = 0;
        GLint max_candidates = -1;
        GLint local_size = -1;
    };

    void detect_pair(const GarmentBufferRanges& upper_range,
                     const GarmentBvhLayout& upper_layout,
                     const GarmentBufferRanges& lower_range,
                     const GarmentBvhLayout& lower_layout,
                     const CollisionCandidateBuffer& collision_candidates,
                     QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    CandidateDetectionProgram candidate_detect_;
    DispatchSizeProgram dispatch_size_;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    mutable GpuElapsedTimer candidate_detect_timer_;
#endif
};
