#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothBvhResources.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionCandidateBuffers.h"

struct SimulationGpuViews final
{
    ClothMotionBufferView cloth_motion;
    ClothCollisionPushoutBufferView cloth_collision_pushout;
    ClothContactMotionBufferView cloth_contact_motion;
    ClothBodyTriangleIdBufferView cloth_body_triangle_ids;
    ClothMeshTopologyResources cloth_topology;
    ClothBvhBufferView cloth_bvh;
    const std::vector<GarmentBufferRanges>* garment_buffer_ranges = nullptr;
    CharacterMeshTopologyResources body_topology;
    CharacterVertexBufferView body_vertices;
    TriangleGeometryResources body_triangle_geometry;
    TriangleBvhResources body_triangle_bvh;
    VertexBvhResources body_vertex_bvh;
    EdgeBvhResources body_edge_bvh;
    CollisionCandidateBufferView collision_candidates;
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;
    AttachmentConstraintBufferView attachment_constraints;
};
