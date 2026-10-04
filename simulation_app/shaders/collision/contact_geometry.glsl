#ifndef COLLISION_GEOMETRY_COMMON_GLSL
#define COLLISION_GEOMETRY_COMMON_GLSL

#include "../mesh/primitive_geometry.glsl"

const float penetration_tolerance = 0.0005;
const float segment_parallel_tolerance = 1.0e-8;
const float max_float = 3.402823e+38;

struct SegmentState {
    EdgePositions positions;
    float t;
    vec3 point;
};

bool compute_triangle_normal(TrianglePositions triangle, out vec3 normal)
{
    vec4 result = triangle_normal(triangle.a, triangle.b, triangle.c);
    normal = result.xyz;
    return result.w > 0.0;
}

vec3 align_normal(vec3 normal, vec3 reference_normal)
{
    return dot(normal, reference_normal) < 0.0 ? -normal : normal;
}

bool compute_barycentric(vec3 point, TrianglePositions triangle, out vec3 barycentric)
{
    vec3 ab = triangle.b - triangle.a;
    vec3 ac = triangle.c - triangle.a;
    vec3 ap = point - triangle.a;

    float d00 = dot(ab, ab);
    float d01 = dot(ab, ac);
    float d11 = dot(ac, ac);
    float d20 = dot(ap, ab);
    float d21 = dot(ap, ac);
    float denominator = d00 * d11 - d01 * d01;
    if (denominator <= 1.0e-20) {
        return false;
    }

    barycentric.z = (d00 * d21 - d01 * d20) / denominator;
    barycentric.y = (d11 * d20 - d01 * d21) / denominator;
    barycentric.x = 1.0 - barycentric.y - barycentric.z;
    return true;
}

bool is_inside_triangle(vec3 barycentric)
{
    return barycentric.x >= 0.0 && barycentric.y >= 0.0 && barycentric.z >= 0.0;
}

struct ClothVertexFaceContact {
    vec3 correction_normal;
    vec3 barycentric;
    float depth;
};

bool compute_cloth_boundary_contact(vec3 point,
                                     TrianglePositions face,
                                     vec3 face_normal,
                                     float collision_thickness,
                                     out ClothVertexFaceContact contact)
{
    vec3 closest_point = closest_point_on_triangle(point, face);
    vec3 separation = point - closest_point;
    float distance = length(separation);
    if (distance >= collision_thickness ||
        !compute_barycentric(closest_point, face, contact.barycentric) ||
        !is_inside_triangle(contact.barycentric)) {
        return false;
    }

    contact.correction_normal = distance > 0.0 ? separation / distance : face_normal;
    contact.depth = collision_thickness - distance;
    return true;
}

bool compute_cloth_vertex_face_contact(uint vertex_index,
                                      uvec3 face_vertices,
                                      float collision_thickness,
                                      out ClothVertexFaceContact contact)
{
    vec3 previous_vertex_position = read_cloth_previous_position(vertex_index);
    vec3 current_vertex_position = read_cloth_current_position(vertex_index);
    TrianglePositions previous_face = read_cloth_previous_triangle(face_vertices);
    TrianglePositions current_face = read_cloth_current_triangle(face_vertices);

    const float distance_delta_epsilon = 1.0e-8;

    vec3 previous_normal;
    vec3 current_normal;
    if (!compute_triangle_normal(previous_face, previous_normal) ||
        !compute_triangle_normal(current_face, current_normal)) {
        return false;
    }

    float previous_distance = dot(previous_vertex_position - previous_face.a, previous_normal);
    float current_distance = dot(current_vertex_position - current_face.a, current_normal);
    float signed_distance = abs(previous_distance) <= distance_delta_epsilon ? current_distance : previous_distance;
    float normal_sign = signed_distance < 0.0 ? -1.0 : 1.0;
    previous_distance *= normal_sign;
    current_distance *= normal_sign;
    current_normal *= normal_sign;

    bool has_swept_contact = false;
    float distance_delta = previous_distance - current_distance;
    // Inside the thickness shell, sweep against the face itself.
    float contact_distance = previous_distance > collision_thickness ? collision_thickness : 0.0;
    if (current_distance <= contact_distance &&
        distance_delta > distance_delta_epsilon) {
        float hit_time = clamp((previous_distance - contact_distance) / distance_delta, 0.0, 1.0);
        vec3 hit_vertex_position = mix(previous_vertex_position, current_vertex_position, hit_time);
        TrianglePositions hit_face = interpolate_triangle_positions(previous_face, current_face, hit_time);
        has_swept_contact = compute_barycentric(hit_vertex_position, hit_face, contact.barycentric) &&
                            is_inside_triangle(contact.barycentric);
    }

    if (!has_swept_contact) {
        return compute_cloth_boundary_contact(current_vertex_position,
                                               current_face,
                                               current_normal,
                                               collision_thickness,
                                               contact);
    }

    contact.depth = collision_thickness - current_distance;
    if (contact.depth <= 0.0) {
        return false;
    }

    contact.correction_normal = current_normal;
    return true;
}

void update_closest_segments(float first_t,
                             float second_t,
                             inout SegmentState first,
                             inout SegmentState second,
                             inout float best_distance_sq)
{
    vec3 candidate_first_point = interpolate_edge_position(first.positions, first_t);
    vec3 candidate_second_point = interpolate_edge_position(second.positions, second_t);
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
    vec3 first_direction = first.positions.b - first.positions.a;
    vec3 second_direction = second.positions.b - second.positions.a;
    vec3 segment_delta = first.positions.a - second.positions.a;
    float first_length_sq = dot(first_direction, first_direction);
    float second_length_sq = dot(second_direction, second_direction);
    if (first_length_sq <= 1.0e-8 || second_length_sq <= 1.0e-8) {
        return false;
    }

    float direction_dot = dot(first_direction, second_direction);
    float first_delta_dot = dot(first_direction, segment_delta);
    float second_delta_dot = dot(second_direction, segment_delta);
    float direction_determinant = first_length_sq * second_length_sq - direction_dot * direction_dot;

    // parallel edges
    if (direction_determinant <= segment_parallel_tolerance * first_length_sq * second_length_sq) {
        float best_distance_sq = max_float;
        update_closest_segments(0.0,
                                clamp(second_delta_dot / second_length_sq, 0.0, 1.0),
                                first,
                                second,
                                best_distance_sq);
        update_closest_segments(1.0,
                                clamp((direction_dot + second_delta_dot) / second_length_sq, 0.0, 1.0),
                                first,
                                second,
                                best_distance_sq);
        update_closest_segments(clamp(-first_delta_dot / first_length_sq, 0.0, 1.0),
                                0.0,
                                first,
                                second,
                                best_distance_sq);
        update_closest_segments(clamp((direction_dot - first_delta_dot) / first_length_sq, 0.0, 1.0),
                                1.0,
                                first,
                                second,
                                best_distance_sq);

        return true;
    }

    // non-parallel edges
    first.t = clamp((direction_dot * second_delta_dot - first_delta_dot * second_length_sq) / direction_determinant,
                    0.0,
                    1.0);
    second.t = (direction_dot * first.t + second_delta_dot) / second_length_sq;
    if (second.t < 0.0 || second.t > 1.0) {
        second.t = clamp(second.t, 0.0, 1.0);
        first.t = clamp((direction_dot * second.t - first_delta_dot) / first_length_sq, 0.0, 1.0);
    }

    first.point = first.positions.a + first_direction * first.t;
    second.point = second.positions.a + second_direction * second.t;
    return true;
}

#endif
