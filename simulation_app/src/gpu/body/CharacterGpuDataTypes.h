#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterBufferSet final {
    GLuint all_frame_position = 0;
    GLuint previous_position = 0;
    GLuint current_position = 0;
    GLuint triangle_index = 0;
    GLuint character_bvh_node = 0;
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint triangle_geometry = 0;
    GLuint vertex_normal = 0;
};

struct CharacterMeshTopologyResources final {
    GLuint triangle_index_buffer = 0;
    GLuint adjacent_triangle_offsets_buffer = 0;
    GLuint adjacent_triangle_indices_buffer = 0;

    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
};

struct CharacterAnimationBufferView final {
    GLuint position_buffer = 0;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
};

struct CharacterVertexBufferView final {
    GLuint previous_position_buffer = 0;
    GLuint current_position_buffer = 0;
    GLuint vertex_normal_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct TriangleGeometryResources final {
    GLuint triangle_geometry_buffer = 0;
    std::uint32_t triangle_count = 0;
};

struct CharacterNormalResources final {
    GLuint triangle_geometry_buffer = 0;
    GLuint vertex_normal_buffer = 0;
    std::uint32_t triangle_count = 0;
};

struct MeshBvhResources final {
    GLuint node_buffer = 0;
    std::uint32_t node_count = 0;
    std::uint32_t root_node_index = 0;
};
