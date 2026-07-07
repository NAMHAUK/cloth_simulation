#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/CollisionContactBuffers.h"

inline bool is_valid_motion_view(const ClothMotionBufferView& motion_view)
{
    return motion_view.current_position_buffer != 0 &&
           motion_view.previous_position_buffer != 0 &&
           motion_view.velocity_buffer != 0 &&
           motion_view.vertex_count != 0;
}

inline bool is_valid_collision_state_view(const ClothCollisionStateBufferView& collision_view)
{
    return collision_view.collision_state_buffer != 0 &&
           collision_view.vertex_count != 0;
}

inline bool is_valid_character_vertex_buffer_view(const CharacterVertexBufferView& vertex_view)
{
    return vertex_view.previous_position_buffer != 0 &&
           vertex_view.current_position_buffer != 0 &&
           vertex_view.vertex_normal_buffer != 0 &&
           vertex_view.vertex_count != 0;
}

inline bool is_valid_cloth_mesh_topology_resource(const ClothMeshTopologyResources& topology)
{
    return topology.triangle_index_buffer != 0 &&
           topology.vertex_count != 0 &&
           topology.triangle_count != 0;
}

inline bool is_valid_character_mesh_topology_resource(const CharacterMeshTopologyResources& topology)
{
    return topology.triangle_index_buffer != 0 &&
           topology.vertex_count != 0 &&
           topology.triangle_count != 0;
}

inline bool is_valid_distance_constraint_view(const DistanceConstraintBufferView& constraint_view)
{
    return constraint_view.edge_index_buffer != 0 &&
           constraint_view.rest_length_buffer != 0 &&
           constraint_view.constraint_count != 0 &&
           constraint_view.color_ranges != nullptr &&
           !constraint_view.color_ranges->empty();
}

inline bool is_valid_triangle_geometry_resource(const TriangleGeometryResources& triangle_geometry)
{
    return triangle_geometry.triangle_geometry_buffer != 0 &&
           triangle_geometry.triangle_count != 0;
}

inline bool is_valid_triangle_bvh_resource(const TriangleBvhResources& triangle_bvh)
{
    return triangle_bvh.node_buffer != 0 &&
           triangle_bvh.node_count != 0;
}

inline bool is_valid_vertex_bvh_resource(const VertexBvhResources& vertex_bvh)
{
    return vertex_bvh.node_buffer != 0 &&
           vertex_bvh.vertex_id_buffer != 0 &&
           vertex_bvh.node_count != 0;
}

inline bool is_valid_edge_bvh_resource(const EdgeBvhResources& edge_bvh)
{
    return edge_bvh.node_buffer != 0 &&
           edge_bvh.edge_index_buffer != 0 &&
           edge_bvh.node_count != 0 &&
           edge_bvh.edge_count != 0;
}

inline bool is_valid_contact_pair_buffers(const ContactPairBuffers& contact_pair_buffers)
{
    return contact_pair_buffers.pairs != 0 &&
           contact_pair_buffers.pair_count != 0 &&
           contact_pair_buffers.dispatch_size != 0 &&
           contact_pair_buffers.overflow_count != 0 &&
           contact_pair_buffers.capacity != 0;
}

inline bool is_valid_collision_contact_buffer_view(const CollisionContactBufferView& collision_contact_view)
{
    return is_valid_contact_pair_buffers(collision_contact_view.cloth_vertex_body_face) &&
           is_valid_contact_pair_buffers(collision_contact_view.cloth_edge_body_edge) &&
           is_valid_contact_pair_buffers(collision_contact_view.cloth_face_body_vertex) &&
           collision_contact_view.correction_sum_buffer != 0 &&
           collision_contact_view.vertex_capacity != 0;
}
