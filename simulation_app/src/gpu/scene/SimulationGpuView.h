#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothBvhResources.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionCandidateBuffers.h"

#include <array>

struct SimulationGpuView final
{
    // Cloth state
    ClothMotionBufferView cloth_motion;
    ClothCollisionPushoutBufferView cloth_collision_pushout;
    ClothContactMotionBufferView cloth_contact_motion;
    ClothBodyTriangleIdBufferView cloth_body_triangle_ids;
    ClothMeshTopologyResources cloth_topology;
    ClothBvhBufferView cloth_bvh;
    std::array<ElementRange, 2> garment_vertex_ranges{};
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;
    AttachmentConstraintBufferView attachment_constraints;

    // Body collision state
    CharacterMeshTopologyResources body_topology;
    CharacterVertexBufferView body_vertices;
    TriangleGeometryResources body_triangle_geometry;
    TriangleBvhResources body_triangle_bvh;
    VertexBvhResources body_vertex_bvh;
    EdgeBvhResources body_edge_bvh;

    // Collision workspace
    CollisionCandidateBufferView collision_candidates;
};
