#pragma once

#include "gpu/scene/CollisionCandidateBuffers.h"
#include "gpu/scene/SimulationGpuView.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothBodyCollisionDetector final
{
public:
    explicit ClothBodyCollisionDetector(float collision_thickness);
    ClothBodyCollisionDetector(const ClothBodyCollisionDetector&) = delete;
    ClothBodyCollisionDetector& operator=(const ClothBodyCollisionDetector&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuView& views) const;
    void detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
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
        GLint local_size = -1;
    };

    void detect_cloth_vertex_body_face_collision_candidates(
        const ClothMotionBufferView& motion_view,
        const BvhBufferView& body_triangle_bvh,
        const CollisionCandidateBuffer& collision_candidates,
        QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_edge_body_edge_collision_candidates(
        const ClothMotionBufferView& motion_view,
        const DistanceConstraintBufferView& cloth_edges,
        const BvhBufferView& body_edge_bvh,
        const CollisionCandidateBuffer& collision_candidates,
        QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_face_body_vertex_collision_candidates(
        const ClothMotionBufferView& motion_view,
        const ClothMeshTopologyResources& cloth_topology,
        GLuint body_vertex_index_buffer,
        const BvhBufferView& body_vertex_bvh,
        const CollisionCandidateBuffer& collision_candidates,
        QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;
    bool has_programs() const;

    CandidateDetectionProgram cloth_vertex_body_face_;
    CandidateDetectionProgram cloth_edge_body_edge_;
    CandidateDetectionProgram cloth_face_body_vertex_;
    DispatchSizeProgram dispatch_size_;
    float collision_thickness_ = 0.0f;
};
