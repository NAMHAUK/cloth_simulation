#ifndef PRIMITIVE_GEOMETRY_GLSL
#define PRIMITIVE_GEOMETRY_GLSL

struct EdgePositions {
    vec3 a;
    vec3 b;
};

struct TrianglePositions {
    vec3 a;
    vec3 b;
    vec3 c;
};

vec4 triangle_normal(vec3 a, vec3 b, vec3 c)
{
    vec3 normal = cross(b - a, c - a);
    float normal_length = length(normal);
    return normal_length > 1.0e-10 ? vec4(normal / normal_length, normal_length) : vec4(0.0);
}

EdgePositions interpolate_edge_positions(EdgePositions previous, EdgePositions current, float time)
{
    return EdgePositions(mix(previous.a, current.a, time),
                         mix(previous.b, current.b, time));
}

TrianglePositions interpolate_triangle_positions(TrianglePositions previous, TrianglePositions current, float time)
{
    return TrianglePositions(mix(previous.a, current.a, time),
                             mix(previous.b, current.b, time),
                             mix(previous.c, current.c, time));
}

vec3 interpolate_edge_position(EdgePositions edge, float t)
{
    return mix(edge.a, edge.b, t);
}

vec3 interpolate_triangle_position(TrianglePositions triangle, vec3 barycentric)
{
    return triangle.a * barycentric.x + triangle.b * barycentric.y + triangle.c * barycentric.z;
}

#endif
