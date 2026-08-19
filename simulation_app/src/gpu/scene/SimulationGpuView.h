#pragma once

#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionCandidateBuffers.h"

#include <array>

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

    // Collision workspace
    CollisionCandidateBufferView collision_candidates;
};
