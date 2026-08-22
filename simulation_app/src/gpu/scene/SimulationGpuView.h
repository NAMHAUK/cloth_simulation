#pragma once

#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CollisionCandidateBuffer final
{
    GLuint candidates = 0;
    GLuint candidate_count = 0;
    GLuint dispatch_size = 0;
    GLuint overflow_count = 0;
    std::uint32_t capacity = 0;
};

struct CollisionBuffers final
{
    CollisionCandidateBuffer cloth_vertex_body_face;
    CollisionCandidateBuffer cloth_edge_body_edge;
    CollisionCandidateBuffer cloth_face_body_vertex;
    CollisionCandidateBuffer cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    GLuint contact_motion_delta_sum_buffer = 0;
};

struct SimulationGpuView final
{
    explicit SimulationGpuView(const std::array<GarmentBufferState, 2>& garment_states)
        : garment_buffer_states(garment_states)
    {}

    // Cloth state
    ClothMotionBufferView cloth_motion;
    ClothCollisionPushoutBufferView cloth_collision_pushout;
    ClothContactMotionBufferView cloth_contact_motion;
    ClothBodyTriangleIndexBufferView cloth_body_triangle_indices;
    ClothMeshTopologyResources cloth_topology;
    BvhBufferView cloth_bvh;
    const std::array<GarmentBufferState, 2>& garment_buffer_states;
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;
    AttachmentConstraintBufferView attachment_constraints;

    // Body collision state
    CharacterMeshTopologyResources body_topology;
    CharacterVertexBufferView body_vertices;
    TriangleGeometryResources body_triangle_geometry;
    BvhBufferView body_triangle_bvh;
    BvhBufferView body_vertex_bvh;
    BvhBufferView body_edge_bvh;

    // Collision state
    CollisionBuffers collision_candidates;
};
