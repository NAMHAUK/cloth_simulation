#ifndef POSITION_IO_GLSL
#define POSITION_IO_GLSL

#include "primitive_geometry.glsl"

#ifdef POSITION_IO_CLOTH_CURRENT
vec3 read_cloth_current_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(cloth_current_positions[base_index],
                cloth_current_positions[base_index + 1u],
                cloth_current_positions[base_index + 2u]);
}

EdgePositions read_cloth_current_edge(uvec2 vertex_indices)
{
    return EdgePositions(read_cloth_current_position(vertex_indices.x),
                         read_cloth_current_position(vertex_indices.y));
}

TrianglePositions read_cloth_current_triangle(uvec3 vertex_indices)
{
    return TrianglePositions(read_cloth_current_position(vertex_indices.x),
                             read_cloth_current_position(vertex_indices.y),
                             read_cloth_current_position(vertex_indices.z));
}
#endif

#ifdef POSITION_IO_CLOTH_PREVIOUS
vec3 read_cloth_previous_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(cloth_previous_positions[base_index],
                cloth_previous_positions[base_index + 1u],
                cloth_previous_positions[base_index + 2u]);
}

EdgePositions read_cloth_previous_edge(uvec2 vertex_indices)
{
    return EdgePositions(read_cloth_previous_position(vertex_indices.x),
                         read_cloth_previous_position(vertex_indices.y));
}

TrianglePositions read_cloth_previous_triangle(uvec3 vertex_indices)
{
    return TrianglePositions(read_cloth_previous_position(vertex_indices.x),
                             read_cloth_previous_position(vertex_indices.y),
                             read_cloth_previous_position(vertex_indices.z));
}
#endif

#ifdef POSITION_IO_BODY_CURRENT
vec3 read_body_current_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(body_current_positions[base_index],
                body_current_positions[base_index + 1u],
                body_current_positions[base_index + 2u]);
}

EdgePositions read_body_current_edge(uvec2 vertex_indices)
{
    return EdgePositions(read_body_current_position(vertex_indices.x),
                         read_body_current_position(vertex_indices.y));
}

TrianglePositions read_body_current_triangle(uvec3 vertex_indices)
{
    return TrianglePositions(read_body_current_position(vertex_indices.x),
                             read_body_current_position(vertex_indices.y),
                             read_body_current_position(vertex_indices.z));
}
#endif

#ifdef POSITION_IO_BODY_PREVIOUS
vec3 read_body_previous_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(body_previous_positions[base_index],
                body_previous_positions[base_index + 1u],
                body_previous_positions[base_index + 2u]);
}

EdgePositions read_body_previous_edge(uvec2 vertex_indices)
{
    return EdgePositions(read_body_previous_position(vertex_indices.x),
                         read_body_previous_position(vertex_indices.y));
}

TrianglePositions read_body_previous_triangle(uvec3 vertex_indices)
{
    return TrianglePositions(read_body_previous_position(vertex_indices.x),
                             read_body_previous_position(vertex_indices.y),
                             read_body_previous_position(vertex_indices.z));
}
#endif

#ifdef POSITION_IO_CLOTH_WRITE
void write_cloth_current_position(uint vertex_index, vec3 position)
{
    uint base_index = vertex_index * 3u;
    cloth_current_positions[base_index] = position.x;
    cloth_current_positions[base_index + 1u] = position.y;
    cloth_current_positions[base_index + 2u] = position.z;
}
#endif

#endif
