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

struct TriangleGeometry {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 normal;
};

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
