#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct ClothBufferSet final
{
    GLuint vao = 0;
    GLuint current_position = 0;
    GLuint previous_position = 0;
    GLuint collision_pushout = 0;
    GLuint cloth_cloth_pushout = 0;
    GLuint contact_motion_delta = 0;
    GLuint body_triangle_index = 0;
    GLuint triangle_vertex_indices = 0;
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint stretch_edge_index = 0;
    GLuint stretch_rest_length = 0;
    GLuint bending_edge_index = 0;
    GLuint bending_rest_length = 0;
    GLuint attachment_indices = 0;
    GLuint attachment_barycentric_offset = 0;
    GLuint triangle_normal = 0;
    GLuint vertex_normal = 0;
    GLuint bvh_node = 0;
    GLuint triangle_bounds = 0;
};

struct ClothBufferElementCounts final
{
    std::uint32_t vertex = 0;
    std::uint32_t triangle = 0;
    std::uint32_t stretch_constraint = 0;
    std::uint32_t bending_constraint = 0;
    std::uint32_t attachment_constraint = 0;
};

struct GarmentBufferState final
{
    std::uint32_t vertex_start_index = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_start_index = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t stretch_constraint_start_index = 0;
    std::uint32_t stretch_constraint_count = 0;
    std::uint32_t bending_constraint_start_index = 0;
    std::uint32_t bending_constraint_count = 0;
    std::uint32_t attachment_constraint_start_index = 0;
    std::uint32_t attachment_constraint_count = 0;
    std::uint32_t active_attachment_constraint_count = 0;
    std::uint32_t bvh_root_node_index = 0;
    std::vector<std::uint32_t> bvh_level_offsets;
};

struct ClothMotionBufferView final
{
    GLuint current_position_buffer = 0;
    GLuint previous_position_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct ClothCollisionPushoutBufferView final
{
    GLuint collision_pushout_buffer = 0;
    GLuint cloth_cloth_pushout_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct ClothContactMotionBufferView final
{
    GLuint contact_motion_delta_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct ClothBodyTriangleIndexBufferView final
{
    GLuint body_triangle_index_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct ClothMeshTopologyResources final
{
    GLuint position_buffer = 0;
    GLuint triangle_index_buffer = 0;
    GLuint adjacent_triangle_offsets_buffer = 0;
    GLuint adjacent_triangle_indices_buffer = 0;

    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
};

struct ClothNormalResources final
{
    GLuint triangle_normal_buffer = 0;
    GLuint vertex_normal_buffer = 0;
};

struct ClothBvhBufferView final
{
    GLuint node_buffer = 0;
    GLuint triangle_bounds_buffer = 0;
    std::uint32_t node_count = 0;
    std::uint32_t garment_count = 0;
};

struct DistanceConstraintBufferView final
{
    GLuint edge_index_buffer = 0;
    GLuint rest_length_buffer = 0;
    std::uint32_t constraint_count = 0;
    const std::vector<ConstraintColorState>* color_states = nullptr;
};

struct AttachmentConstraintBufferView final
{
    GLuint attachment_index_buffer = 0;
    GLuint barycentric_offset_buffer = 0;
    std::uint32_t constraint_count = 0;
};
