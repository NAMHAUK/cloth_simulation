#ifndef COLLISION_GEOMETRY_COMMON_GLSL
#define COLLISION_GEOMETRY_COMMON_GLSL

#include "../mesh/primitive_geometry.glsl"

const float triangle_area_sq_epsilon = 1.0e-20;
const float triangle_edge_tolerance = -1.0e-6;
const float penetration_tolerance = 0.0005;
const float segment_length_sq_epsilon = 1.0e-8;
const float segment_parallel_tolerance = 1.0e-8;
const float max_float = 3.402823e+38;

struct SegmentState {
    EdgePositions positions;
    float t;
    vec3 point;
};

bool compute_triangle_normal(TrianglePositions triangle, out vec3 normal)
{
    normal = cross(triangle.b - triangle.a, triangle.c - triangle.a);
    float normal_length_sq = dot(normal, normal);
    if (normal_length_sq <= triangle_area_sq_epsilon) {
        return false;
    }

    normal *= inversesqrt(normal_length_sq);
    return true;
}

vec3 align_normal(vec3 normal, vec3 reference_normal)
{
    return dot(normal, reference_normal) < 0.0 ? -normal : normal;
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

bool compute_barycentric_if_inside(vec3 point, TrianglePositions triangle, out vec3 barycentric)
{
    return compute_barycentric_if_inside(point, triangle.a, triangle.b, triangle.c, barycentric);
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
    vec3 closest_point = closest_point_on_triangle(point, face.a, face.b, face.c);
    vec3 separation = point - closest_point;
    float distance = length(separation);
    if (distance >= collision_thickness ||
        !compute_barycentric_if_inside(closest_point, face, contact.barycentric)) {
        return false;
    }

    contact.barycentric = max(contact.barycentric, vec3(0.0));
    contact.barycentric /= dot(contact.barycentric, vec3(1.0));
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

    // Use the starting side only for this sweep and the zero-distance fallback.
    float signed_distance = dot(previous_vertex_position - previous_face.a, previous_normal);
    if (abs(signed_distance) <= distance_delta_epsilon) {
        signed_distance = dot(current_vertex_position - current_face.a, current_normal);
    }
    float normal_sign = signed_distance < 0.0 ? -1.0 : 1.0;
    previous_normal *= normal_sign;
    current_normal *= normal_sign;
    float previous_distance = dot(previous_vertex_position - previous_face.a, previous_normal);
    float current_distance = dot(current_vertex_position - current_face.a, current_normal);

    bool has_swept_contact = false;
    float distance_delta = previous_distance - current_distance;
    // Inside the thickness shell, sweep against the face itself.
    float contact_distance = previous_distance > collision_thickness ? collision_thickness : 0.0;
    if (previous_distance >= contact_distance &&
        current_distance <= contact_distance &&
        distance_delta > distance_delta_epsilon) {
        float hit_time = clamp((previous_distance - contact_distance) / distance_delta, 0.0, 1.0);
        vec3 hit_vertex_position = mix(previous_vertex_position, current_vertex_position, hit_time);
        TrianglePositions hit_face = interpolate_triangle_positions(previous_face, current_face, hit_time);
        vec3 hit_normal;
        if (compute_triangle_normal(hit_face, hit_normal)) {
            hit_normal *= normal_sign;
            float hit_distance = dot(hit_vertex_position - hit_face.a, hit_normal);
            vec3 hit_surface_point = hit_vertex_position - hit_normal * hit_distance;
            has_swept_contact = compute_barycentric_if_inside(hit_surface_point, hit_face, contact.barycentric);
        }
    }

    if (!has_swept_contact) {
        return compute_cloth_boundary_contact(current_vertex_position,
                                               current_face,
                                               current_normal,
                                               collision_thickness,
                                               contact);
    }

    vec3 current_surface_point = interpolate_triangle_position(current_face, contact.barycentric);
    contact.depth = collision_thickness - dot(current_vertex_position - current_surface_point, current_normal);
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

    first.point = first.positions.a + first_direction * first.t;
    second.point = second.positions.a + second_direction * second.t;
    return true;
}

#endif
