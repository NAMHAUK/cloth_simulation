#ifndef POSITION_IO_GLSL
#define POSITION_IO_GLSL

#include "primitive_geometry.glsl"

#ifdef POSITION_IO_CLOTH_CURRENT
vec3 read_cloth_current_position(uint vertex_index)
{
    return cloth_current_positions[vertex_index].xyz;
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
    return cloth_previous_positions[vertex_index].xyz;
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
    return body_current_positions[vertex_index].xyz;
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
    return body_previous_positions[vertex_index].xyz;
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
    cloth_current_positions[vertex_index] = vec4(position, 0.0);
}
#endif

#endif
