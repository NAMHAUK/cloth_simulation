#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterBufferSet final
{
    GLuint all_frame_position = 0;
    GLuint previous_position = 0;
    GLuint current_position = 0;
    GLuint triangle_index = 0;
    GLuint body_triangle_bvh_node = 0;
    GLuint body_triangle_bounds = 0;
    GLuint body_vertex_bvh_node = 0;
    GLuint body_vertex_bvh_vertex_id = 0;
    GLuint body_vertex_bounds = 0;
    GLuint body_edge_bvh_node = 0;
    GLuint body_edge_index = 0;
    GLuint body_edge_bounds = 0;
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint triangle_geometry = 0;
    GLuint vertex_normal = 0;
};

struct CharacterMeshTopologyResources final
{
    GLuint triangle_index_buffer = 0;
    GLuint adjacent_triangle_offsets_buffer = 0;
    GLuint adjacent_triangle_indices_buffer = 0;

    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
};

struct CharacterAnimationBufferView final
{
    GLuint position_buffer = 0;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
};

struct CharacterVertexBufferView final
{
    GLuint previous_position_buffer = 0;
    GLuint current_position_buffer = 0;
    GLuint vertex_normal_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct TriangleGeometryResources final
{
    GLuint triangle_geometry_buffer = 0;
    std::uint32_t triangle_count = 0;
};

struct CharacterNormalResources final
{
    GLuint triangle_geometry_buffer = 0;
    GLuint vertex_normal_buffer = 0;
    std::uint32_t triangle_count = 0;
};

struct TriangleBvhResources final
{
    GLuint node_buffer = 0;
    GLuint triangle_bounds_buffer = 0;
    std::uint32_t node_count = 0;
    std::uint32_t triangle_count = 0;
};

struct VertexBvhResources final
{
    GLuint node_buffer = 0;
    GLuint vertex_id_buffer = 0;
    GLuint vertex_bounds_buffer = 0;
    std::uint32_t node_count = 0;
};

struct EdgeBvhResources final
{
    GLuint node_buffer = 0;
    GLuint edge_index_buffer = 0;
    GLuint edge_bounds_buffer = 0;
    std::uint32_t node_count = 0;
    std::uint32_t edge_count = 0;
};
