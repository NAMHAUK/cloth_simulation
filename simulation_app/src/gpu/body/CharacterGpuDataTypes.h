#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterMeshTopologyResources final {
    GLuint position_buffer = 0;
    GLuint index_buffer = 0;
    GLuint adjacent_triangle_offsets_buffer = 0;
    GLuint adjacent_triangle_indices_buffer = 0;

    std::uint32_t position_component_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
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
