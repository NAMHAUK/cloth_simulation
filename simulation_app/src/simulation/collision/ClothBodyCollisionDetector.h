#pragma once

#include "gpu/scene/CollisionContactBuffers.h"
#include "simulation/SimulationGpuViews.h"
#include "utils/GpuElapsedTimer.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_GPU_TIMING
#define CLOTH_SIM_COLLISION_GPU_TIMING 1
#endif

class ClothBodyCollisionDetector final {
public:
    ClothBodyCollisionDetector() = default;
    ClothBodyCollisionDetector(const ClothBodyCollisionDetector&) = delete;
    ClothBodyCollisionDetector& operator=(const ClothBodyCollisionDetector&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& cloth_vertex_body_face_generate_shader_path,
                    const std::filesystem::path& cloth_edge_body_edge_generate_shader_path,
                    const std::filesystem::path& cloth_face_body_vertex_generate_shader_path,
                    const std::filesystem::path& dispatch_size_shader_path,
                    float collision_thickness,
                    std::uint32_t ignored_body_part_mask,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_detect(const SimulationGpuViews& views) const;
    void detect(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct PairDetectionProgram final {
        GLuint program = 0;
        GLint item_count = -1;
        GLint max_pairs = -1;
        GLint thickness = -1;
        GLint ignored_body_part_mask = -1;
    };

    struct DispatchSizeProgram final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint local_size = -1;
    };

    void detect_cloth_vertex_body_face_contacts(const ClothMotionBufferView& motion_view,
                                                const TriangleGeometryResources& character_geometry,
                                                const TriangleBvhResources& character_bvh,
                                                const ContactPairBuffers& contact_pairs,
                                                QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_edge_body_edge_contacts(const ClothMotionBufferView& motion_view,
                                              const DistanceConstraintBufferView& cloth_edges,
                                              const CharacterVertexBufferView& character_vertex_view,
                                              const EdgeBvhResources& body_edge_bvh,
                                              const ContactPairBuffers& contact_pairs,
                                              QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_face_body_vertex_contacts(const ClothMotionBufferView& motion_view,
                                                const ClothMeshTopologyResources& cloth_topology,
                                                const CharacterVertexBufferView& character_vertex_view,
                                                const VertexBvhResources& body_vertex_bvh,
                                                const ContactPairBuffers& contact_pairs,
                                                QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(const ContactPairBuffers& contact_pairs, QOpenGLFunctions_4_5_Core& gl) const;
    bool has_programs() const;

    PairDetectionProgram cloth_vertex_body_face_;
    PairDetectionProgram cloth_edge_body_edge_;
    PairDetectionProgram cloth_face_body_vertex_;
    DispatchSizeProgram dispatch_size_;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    mutable GpuElapsedTimer cloth_vertex_body_face_timer_;
    mutable GpuElapsedTimer cloth_edge_body_edge_timer_;
    mutable GpuElapsedTimer cloth_face_body_vertex_timer_;
#endif
    float collision_thickness_ = 0.0f;
    std::uint32_t ignored_body_part_mask_ = 0;
};
