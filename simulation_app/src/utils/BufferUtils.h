#pragma once

#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionCandidateBuffers.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

template <typename T>
constexpr GLsizeiptr byte_size(std::size_t count) noexcept
{
    return static_cast<GLsizeiptr>(count * sizeof(T));
}

inline bool is_valid_buffer_access(std::uint32_t start_index, std::uint32_t count, std::uint32_t total_count)
{
    return count != 0u && start_index <= total_count && count <= total_count - start_index;
}

inline bool is_valid_motion_view(const ClothMotionBufferView& motion_view)
{
    return motion_view.current_position_buffer != 0 &&
           motion_view.previous_position_buffer != 0 &&
           motion_view.vertex_count != 0;
}

inline bool is_valid_collision_pushout_view(const ClothCollisionPushoutBufferView& collision_pushout_view)
{
    return collision_pushout_view.collision_pushout_buffer != 0 &&
           collision_pushout_view.cloth_cloth_pushout_buffer != 0 &&
           collision_pushout_view.vertex_count != 0;
}

inline bool is_valid_contact_motion_view(const ClothContactMotionBufferView& contact_motion_view)
{
    return contact_motion_view.contact_motion_delta_buffer != 0 && contact_motion_view.vertex_count != 0;
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
    return topology.triangle_index_buffer != 0 && topology.vertex_count != 0 && topology.triangle_count != 0;
}

inline bool is_valid_body_triangle_index_view(
    const ClothBodyTriangleIndexBufferView& body_triangle_index_view)
{
    return body_triangle_index_view.body_triangle_index_buffer != 0 &&
           body_triangle_index_view.vertex_count != 0;
}

inline std::uint32_t active_garment_count(const std::array<GarmentBufferState, 2>& garments)
{
    std::uint32_t count = 0u;
    for (const GarmentBufferState& garment : garments) {
        count += garment.vertex_count != 0u;
    }
    return count;
}

inline std::uint32_t garment_bvh_root_node_index(const GarmentBufferState& garment)
{
    return garment.bvh_level_offsets.empty() ? 0u : garment.bvh_level_offsets.front();
}

inline bool is_valid_bvh_buffer_view(const BvhBufferView& view)
{
    return view.node_buffer != 0 && view.bounds_buffer != 0 && view.node_count != 0;
}

inline bool is_valid_character_mesh_topology_resource(const CharacterMeshTopologyResources& topology)
{
    return topology.triangle_index_buffer != 0 &&
           topology.bvh_vertex_index_buffer != 0 &&
           topology.edge_index_buffer != 0 &&
           topology.vertex_count != 0 &&
           topology.triangle_count != 0 &&
           topology.collision_triangle_count != 0 &&
           topology.collision_triangle_count <= topology.triangle_count;
}

inline bool is_valid_distance_constraint_view(const DistanceConstraintBufferView& constraint_view)
{
    return constraint_view.edge_index_buffer != 0 &&
           constraint_view.rest_length_buffer != 0 &&
           constraint_view.constraint_count != 0 &&
           constraint_view.color_states != nullptr &&
           !constraint_view.color_states->empty();
}

inline bool is_valid_triangle_geometry_resource(const TriangleGeometryResources& triangle_geometry)
{
    return triangle_geometry.triangle_geometry_buffer != 0 && triangle_geometry.triangle_count != 0;
}

inline bool is_valid_collision_candidate_buffer(const CollisionCandidateBuffer& collision_candidate_buffer)
{
    return collision_candidate_buffer.candidates != 0 &&
           collision_candidate_buffer.candidate_count != 0 &&
           collision_candidate_buffer.dispatch_size != 0 &&
           collision_candidate_buffer.overflow_count != 0 &&
           collision_candidate_buffer.capacity != 0;
}

inline bool is_valid_collision_candidate_buffer_view(
    const CollisionCandidateBufferView& collision_candidate_view)
{
    return is_valid_collision_candidate_buffer(collision_candidate_view.cloth_vertex_body_face) &&
           is_valid_collision_candidate_buffer(collision_candidate_view.cloth_edge_body_edge) &&
           is_valid_collision_candidate_buffer(collision_candidate_view.cloth_face_body_vertex) &&
           collision_candidate_view.normal_correction_sum_buffer != 0 &&
           collision_candidate_view.friction_correction_sum_buffer != 0 &&
           collision_candidate_view.contact_motion_delta_sum_buffer != 0 &&
           collision_candidate_view.vertex_capacity != 0;
}

inline bool is_valid_cloth_cloth_candidate_buffer_view(
    const CollisionCandidateBufferView& collision_candidate_view)
{
    return is_valid_collision_candidate_buffer(collision_candidate_view.cloth_cloth_vertex_face) &&
           collision_candidate_view.normal_correction_sum_buffer != 0 &&
           collision_candidate_view.vertex_capacity != 0;
}
