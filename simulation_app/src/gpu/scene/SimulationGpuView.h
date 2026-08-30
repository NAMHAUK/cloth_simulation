#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <cstdint>

#include <glm/vec4.hpp>

#include <QOpenGLFunctions_4_5_Core>

struct CollisionCandidateBuffers final
{
    GLuint candidate_buffer = 0;
    GLuint count_buffer = 0;
    GLuint dispatch_size_buffer = 0;
    std::uint32_t max_pairs = 0;
};

struct CollisionBuffers final
{
    CollisionCandidateBuffers cloth_vertex_body_face;
    CollisionCandidateBuffers cloth_edge_body_edge;
    CollisionCandidateBuffers cloth_face_body_vertex;
    CollisionCandidateBuffers cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    GLuint contact_motion_delta_sum_buffer = 0;
};

struct CollisionCandidateBufferView final
{
    GLuint count_buffer = 0;
    GLuint dispatch_size_buffer = 0;
    std::uint32_t max_pairs = 0;
};

struct CollisionBufferView final
{
    CollisionCandidateBufferView cloth_vertex_body_face;
    CollisionCandidateBufferView cloth_edge_body_edge;
    CollisionCandidateBufferView cloth_face_body_vertex;
    CollisionCandidateBufferView cloth_cloth_vertex_face;
    GLuint normal_correction_sum_buffer = 0;
    GLuint friction_correction_sum_buffer = 0;
    GLuint contact_motion_delta_sum_buffer = 0;
};

struct SimulationGpuView final
{
    explicit SimulationGpuView(const std::array<GarmentBufferState, 2>& garment_states)
        : garment_buffer_states(garment_states)
    {}

    bool has_multiple_garments() const
    {
        return garment_buffer_states[GarmentLayer::Lower].vertex_count != 0u &&
               garment_buffer_states[GarmentLayer::Upper].vertex_count != 0u;
    }

    // Cloth state
    ClothMeshTopologyResources cloth_topology;
    const std::array<GarmentBufferState, 2>& garment_buffer_states;
    DistanceConstraintBufferView stretch_constraints;
    DistanceConstraintBufferView bending_constraints;

    // Body collision state
    CharacterMeshTopologyResources body_topology;
    glm::uvec4 body_arm_triangle_ranges{};

    // Collision state
    CollisionBufferView collision;
};
