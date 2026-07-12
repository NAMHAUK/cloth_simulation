#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionPairBuffers.h"

struct SimulationGpuViews final {
    ClothMotionBufferView cloth_motion;
    ClothCollisionPushoutBufferView cloth_collision_pushout;
    ClothMeshTopologyResources cloth_topology;
    CharacterMeshTopologyResources character_topology;
    CharacterVertexBufferView character_vertices;
    TriangleGeometryResources character_geometry;
    TriangleBvhResources character_bvh;
    VertexBvhResources body_vertex_bvh;
    EdgeBvhResources body_edge_bvh;
    CollisionPairBufferView collision_pairs;
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;
    AttachmentConstraintBufferView attachment_constraints;
};
