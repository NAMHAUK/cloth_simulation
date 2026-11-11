#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct ClothRenderBuffers final
{
    GLuint vao = 0;
    GLuint current_position = 0;
    GLuint triangle_vertex_indices = 0;
    GLuint vertex_normal = 0;
};

struct PbdClothBufferSet final
{
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint triangle_normal = 0;
    GLuint triangle_bvh_node = 0;
    GLuint triangle_bounds = 0;
    GLuint vertex_face_exclusions = 0;
    GLuint edge_vertex_indices = 0;
    GLuint edge_bounds = 0;
    GLuint edge_bvh_node = 0;
    GLuint edge_exclusions = 0;
    GLuint previous_position = 0;
    GLuint collision_pushout = 0;
    GLuint inward_motion_correction = 0;
    GLuint stretch_edge_vertex_indices = 0;
    GLuint stretch_rest_length = 0;
    GLuint bending_edge_vertex_indices = 0;
    GLuint bending_rest_length = 0;
    GLuint attachment_indices = 0;
    GLuint attachment_barycentric_offset = 0;
};

struct AttachmentBufferState final
{
    std::uint32_t start_index = 0;
    std::uint32_t count = 0;
    std::uint32_t active_count = 0;
};

struct GarmentBufferState final
{
    std::uint32_t vertex_start_index = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_start_index = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t stretch_constraint_count = 0;
    std::uint32_t bending_constraint_count = 0;
};

struct GarmentCollisionBufferState final
{
    std::uint32_t vertex_face_exclusion_offset = 0;
    std::uint32_t edge_start_index = 0;
    std::uint32_t edge_count = 0;
    std::uint32_t edge_exclusion_offset = 0;
    std::vector<std::uint32_t> triangle_bvh_level_offsets;
    std::vector<std::uint32_t> edge_bvh_level_offsets;
};
