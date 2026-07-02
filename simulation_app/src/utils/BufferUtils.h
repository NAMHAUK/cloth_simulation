#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuResources.h"

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
           collision_view.contact_normal_buffer != 0 &&
           collision_view.max_contacts_per_vertex != 0 &&
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

inline bool is_valid_cloth_triangle_color_view(const ClothTriangleColorView& triangle_color_view)
{
    return triangle_color_view.triangle_id_buffer != 0 &&
           triangle_color_view.triangle_count != 0 &&
           triangle_color_view.color_ranges != nullptr &&
           !triangle_color_view.color_ranges->empty();
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

inline bool is_valid_mesh_bvh_resource(const MeshBvhResources& mesh_bvh)
{
    return mesh_bvh.node_buffer != 0 &&
           mesh_bvh.node_count != 0 &&
           mesh_bvh.root_node_index < mesh_bvh.node_count;
}

inline bool is_valid_body_vertex_bvh_resource(const BodyVertexBvhResources& body_vertex_bvh)
{
    return body_vertex_bvh.node_buffer != 0 &&
           body_vertex_bvh.vertex_id_buffer != 0 &&
           body_vertex_bvh.node_count != 0 &&
           body_vertex_bvh.root_node_index < body_vertex_bvh.node_count;
}
