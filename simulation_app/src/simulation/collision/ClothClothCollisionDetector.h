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
    bool initialize(const std::filesystem::path& pair_detect_shader_path,
                    const std::filesystem::path& dispatch_size_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuViews& views) const;
    void detect(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void log_diagnostics(const SimulationGpuViews& views,
                         std::uint64_t simulation_step,
                         QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct PairDetectionProgram final {
        GLuint program = 0;
        GLint query_vertex_offset = -1;
        GLint query_vertex_count = -1;
        GLint query_bvh_node_offset = -1;
        GLint target_vertex_offset = -1;
        GLint target_triangle_offset = -1;
        GLint target_bvh_node_offset = -1;
        GLint query_role = -1;
        GLint max_pairs = -1;
    };

    struct DispatchSizeProgram final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint local_size = -1;
    };

    void detect_direction(const GarmentBufferRanges& query_range,
                          const GarmentBvhLayout& query_layout,
                          const GarmentBufferRanges& target_range,
                          const GarmentBvhLayout& target_layout,
                          std::uint32_t query_role,
                          const CollisionPairBuffer& collision_pairs,
                          QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionPairBuffer& collision_pairs,
                             QOpenGLFunctions_4_5_Core& gl) const;

    PairDetectionProgram pair_detect_;
    DispatchSizeProgram dispatch_size_;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    mutable GpuElapsedTimer detection_timer_;
#endif
};
