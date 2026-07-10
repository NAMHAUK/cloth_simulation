#ifndef COLLISION_GEOMETRY_COMMON_GLSL
#define COLLISION_GEOMETRY_COMMON_GLSL

const float triangle_area_sq_epsilon = 1.0e-20;
const float triangle_edge_tolerance = -1.0e-6;
const float segment_length_sq_epsilon = 1.0e-8;
const float segment_parallel_tolerance = 1.0e-8;
const float max_float = 3.402823e+38;

struct SegmentState {
    vec3 vertex0;
    vec3 vertex1;
    float t;
    vec3 point;
};

bool compute_triangle_normal(vec3 a, vec3 b, vec3 c, out vec3 normal)
{
    normal = cross(b - a, c - a);
    float normal_length_sq = dot(normal, normal);
    if (normal_length_sq <= triangle_area_sq_epsilon) {
        return false;
    }

    normal *= inversesqrt(normal_length_sq);
    return true;
}

bool compute_barycentric_if_inside(vec3 point, vec3 a, vec3 b, vec3 c, out vec3 barycentric)
{
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 ap = point - a;

    float d00 = dot(ab, ab);
    float d01 = dot(ab, ac);
    float d11 = dot(ac, ac);
    float d20 = dot(ap, ab);
    float d21 = dot(ap, ac);
    float denominator = d00 * d11 - d01 * d01;
    if (denominator <= triangle_area_sq_epsilon) {
        return false;
    }

    barycentric.z = (d00 * d21 - d01 * d20) / denominator;
    barycentric.y = (d11 * d20 - d01 * d21) / denominator;
    barycentric.x = 1.0 - barycentric.y - barycentric.z;
    return barycentric.x >= triangle_edge_tolerance &&
           barycentric.y >= triangle_edge_tolerance &&
           barycentric.z >= triangle_edge_tolerance;
}

void update_closest_segments(float first_t,
                             float second_t,
                             inout SegmentState first,
                             inout SegmentState second,
                             inout float best_distance_sq)
{
    vec3 candidate_first_point = mix(first.vertex0, first.vertex1, first_t);
    vec3 candidate_second_point = mix(second.vertex0, second.vertex1, second_t);
    vec3 candidate_delta = candidate_first_point - candidate_second_point;
    float candidate_distance_sq = dot(candidate_delta, candidate_delta);

    if (candidate_distance_sq < best_distance_sq) {
        best_distance_sq = candidate_distance_sq;
        first.t = first_t;
        second.t = second_t;
        first.point = candidate_first_point;
        second.point = candidate_second_point;
    }
}

bool closest_segment_points(inout SegmentState first, inout SegmentState second)
{
    vec3 first_direction = first.vertex1 - first.vertex0;
    vec3 second_direction = second.vertex1 - second.vertex0;
    vec3 segment_delta = first.vertex0 - second.vertex0;
    float first_length_sq = dot(first_direction, first_direction);
    float second_length_sq = dot(second_direction, second_direction);
    if (first_length_sq <= segment_length_sq_epsilon ||
        second_length_sq <= segment_length_sq_epsilon) {
        return false;
    }

    float direction_dot = dot(first_direction, second_direction);
    float first_delta_dot = dot(first_direction, segment_delta);
    float second_delta_dot = dot(second_direction, segment_delta);
    float direction_determinant = first_length_sq * second_length_sq - direction_dot * direction_dot;
    if (direction_determinant <= segment_parallel_tolerance * first_length_sq * second_length_sq) {
        float best_distance_sq = max_float;
        update_closest_segments(0.0, clamp(second_delta_dot / second_length_sq, 0.0, 1.0),
                                first, second, best_distance_sq);
        update_closest_segments(1.0, clamp((direction_dot + second_delta_dot) / second_length_sq, 0.0, 1.0),
                                first, second, best_distance_sq);
        update_closest_segments(clamp(-first_delta_dot / first_length_sq, 0.0, 1.0), 0.0,
                                first, second, best_distance_sq);
        update_closest_segments(clamp((direction_dot - first_delta_dot) / first_length_sq, 0.0, 1.0), 1.0,
                                first, second, best_distance_sq);

        return true;
    }

    first.t = clamp((direction_dot * second_delta_dot - first_delta_dot * second_length_sq) /
                    direction_determinant,
                    0.0,
                    1.0);
    second.t = (direction_dot * first.t + second_delta_dot) / second_length_sq;
    if (second.t < 0.0) {
        second.t = 0.0;
        first.t = clamp(-first_delta_dot / first_length_sq, 0.0, 1.0);
    } else if (second.t > 1.0) {
        second.t = 1.0;
        first.t = clamp((direction_dot - first_delta_dot) / first_length_sq, 0.0, 1.0);
    }

    first.point = first.vertex0 + first_direction * first.t;
    second.point = second.vertex0 + second_direction * second.t;
    return true;
}

#endif
