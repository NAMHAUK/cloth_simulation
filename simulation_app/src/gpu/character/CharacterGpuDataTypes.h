#pragma once

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterBufferSet final
{
    GLuint all_frame_positions = 0;
    GLuint previous_position = 0;
    GLuint current_position = 0;
    GLuint triangle_index = 0;
    GLuint body_triangle_bvh_node = 0;
    GLuint body_triangle_bounds = 0;
    GLuint body_edge_bvh_node = 0;
    GLuint body_edge_index = 0;
    GLuint body_edge_bounds = 0;
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint triangle_position = 0;
    GLuint triangle_normal = 0;
    GLuint vertex_normal = 0;
};
