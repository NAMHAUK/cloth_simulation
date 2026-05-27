#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct MeshTopologyResources final {
    GLuint position_buffer = 0;
    GLuint index_buffer = 0;
    GLuint adjacency_offset_buffer = 0;
    GLuint adjacency_triangle_buffer = 0;

    std::uint32_t position_component_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
};

struct MeshNormalResources final {
    GLuint triangle_normal_buffer = 0;
    GLuint vertex_normal_buffer = 0;
};
