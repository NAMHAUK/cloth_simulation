#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

// TEMP GPU TIMING: set to 0 or remove the marked blocks after collision profiling.
#define CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING 1

class BodyVertexClothFaceCollisionSolver final {
public:
    BodyVertexClothFaceCollisionSolver() = default;
    BodyVertexClothFaceCollisionSolver(const BodyVertexClothFaceCollisionSolver&) = delete;
    BodyVertexClothFaceCollisionSolver& operator=(const BodyVertexClothFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const ClothMeshTopologyResources& cloth_topology,
                   const ClothTriangleColorView& triangle_color_view,
                   const CharacterVertexBufferView& character_vertex_view,
                   const BodyVertexBvhResources& body_vertex_bvh) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const ClothMeshTopologyResources& cloth_topology,
               const ClothTriangleColorView& triangle_color_view,
               const CharacterVertexBufferView& character_vertex_view,
               const BodyVertexBvhResources& body_vertex_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    static constexpr std::size_t gpu_timing_query_count = 8u;

    void initialize_gpu_timing_queries(QOpenGLFunctions_4_5_Core& gl);
    void release_gpu_timing_queries(QOpenGLFunctions_4_5_Core& gl);
    void collect_gpu_timing_results(QOpenGLFunctions_4_5_Core& gl) const;
    bool begin_gpu_timing(QOpenGLFunctions_4_5_Core& gl) const;
    void end_gpu_timing(QOpenGLFunctions_4_5_Core& gl) const;

    mutable std::array<GLuint, gpu_timing_query_count> gpu_timing_queries_{};
    mutable std::array<bool, gpu_timing_query_count> gpu_timing_query_pending_{};
    mutable std::size_t next_gpu_timing_query_ = 0u;
    mutable bool gpu_timing_active_ = false;
    mutable std::uint64_t gpu_timing_sample_count_ = 0u;
    mutable std::uint32_t gpu_timing_window_sample_count_ = 0u;
    mutable double gpu_timing_window_total_ms_ = 0.0;
#endif

    GLuint program_ = 0;
    GLint triangle_color_offset_location_ = -1;
    GLint triangle_dispatch_count_location_ = -1;
    GLint root_node_index_location_ = -1;
    GLint max_contacts_per_vertex_location_ = -1;
    GLint collision_thickness_location_ = -1;
    GLint max_correction_length_location_ = -1;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
};
