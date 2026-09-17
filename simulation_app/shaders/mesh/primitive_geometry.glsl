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

vec3 closest_point_on_triangle(vec3 point, vec3 a, vec3 b, vec3 c)
{
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 ap = point - a;
    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return a;
    }

    vec3 bp = point - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        return b;
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    vec3 cp = point - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        return c;
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (ac - ab);
    }

    float inverse_denominator = 1.0 / (va + vb + vc);
    float v = vb * inverse_denominator;
    float w = vc * inverse_denominator;
    return a + ab * v + ac * w;
}

#endif
