#ifndef COLLISION_GEOMETRY_COMMON_GLSL
#define COLLISION_GEOMETRY_COMMON_GLSL

#include "../mesh/primitive_geometry.glsl"

const float segment_parallel_tolerance = 1.0e-8;
const float max_float = 3.402823e+38;

struct SegmentState {
    EdgePositions positions;
    float t;
    vec3 point;
};

struct EdgeContact
{
    float time;
    float edge_fraction;
};

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

bool has_contact(float contact_time)
{
    return contact_time != max_float;
}

float find_thickness_contact_time(vec3 offset, vec3 displacement, float thickness)
{
    // Already within thickness
    float c = dot(offset, offset) - thickness * thickness;
    if (c <= 0.0) {
        return 0.0;
    }

    // don't move || not approaching
    float a = dot(displacement, displacement);
    float b = dot(offset, displacement);
    if (a <= 0.0 || b >= 0.0) {
        return max_float;
    }

    // no intersection
    float discriminant = b * b - a * c;
    if (discriminant < 0.0) {
        return max_float;
    }

    // contact along the path
    float contact_time = c / (sqrt(discriminant) - b);

    return contact_time <= 1.0 ? contact_time : max_float;
}

EdgeContact find_edge_contact(vec3 vertex_start_pos,
                              vec3 vertex_displacement,
                              EdgePositions edge,
                              float thickness)
{
    vec3 edge_vector = edge.b - edge.a;
    float edge_length_sq = dot(edge_vector, edge_vector);

    vec3 offset = vertex_start_pos - edge.a;
    float edge_parameter = dot(offset, edge_vector) / edge_length_sq;
    float edge_parameter_delta = dot(vertex_displacement, edge_vector) / edge_length_sq;

    // find cylinder contact (edge is axis)
    vec3 radial_offset = offset - edge_vector * edge_parameter;
    vec3 radial_displacement = vertex_displacement - edge_vector * edge_parameter_delta;

    float contact_time = find_thickness_contact_time(radial_offset, radial_displacement, thickness);
    if (!has_contact(contact_time)) {
        return EdgeContact(max_float, 0.0);
    }

    // find sphere contact (edge vertex is center)
    float edge_fraction = edge_parameter + edge_parameter_delta * contact_time;
    if (edge_fraction < 0.0 || edge_fraction > 1.0) {
        vec3 sphere_center = edge_fraction < 0.0 ? edge.a : edge.b;

        contact_time = find_thickness_contact_time(vertex_start_pos - sphere_center, vertex_displacement, thickness);
        if (!has_contact(contact_time)) {
            return EdgeContact(max_float, 0.0);
        }
    }

    // compute contact edge fraction
    edge_fraction = clamp(edge_parameter + edge_parameter_delta * contact_time, 0.0, 1.0);
    return EdgeContact(contact_time, edge_fraction);
}

bool find_current_contact_barycentric(vec3 cur_vertex_pos,
                                      TrianglePositions cur_triangle_pos,
                                      vec3 cur_face_normal,
                                      float cur_signed_distance,
                                      out vec3 barycentric)
{
    vec3 projected_cur_vertex_pos = cur_vertex_pos - cur_face_normal * cur_signed_distance;
    return compute_barycentric(projected_cur_vertex_pos, cur_triangle_pos, barycentric) &&
           is_inside_triangle(barycentric);
}

// Approximate triangle motion as translation only (no rotation & scaling)
bool find_contact_barycentric(vec3 relative_prev_vertex_pos,
                              vec3 cur_vertex_pos,
                              TrianglePositions cur_triangle_pos,
                              vec3 cur_face_normal,
                              float cur_signed_distance,
                              float thickness,
                              out vec3 barycentric)
{
    vec3 relative_displacement = cur_vertex_pos - relative_prev_vertex_pos;
    float prev_distance = dot(relative_prev_vertex_pos - cur_triangle_pos.a, cur_face_normal);

    // 1. outside thickness: check current position contact
    if (min(prev_distance, cur_signed_distance) > thickness ||
        max(prev_distance, cur_signed_distance) < -thickness) {
        return find_current_contact_barycentric(cur_vertex_pos,
                                                cur_triangle_pos,
                                                cur_face_normal,
                                                cur_signed_distance,
                                                barycentric);
    }

    // 2.1. within thickness: project start point
    float face_distance = prev_distance;
    float entry_time = 0.0;

    // 2.2. otherwise: project entry point
    if (abs(prev_distance) > thickness) {
        face_distance = sign(prev_distance) * thickness;
        entry_time = (face_distance - prev_distance) / (cur_signed_distance - prev_distance);
    }

    vec3 projected_position = relative_prev_vertex_pos + relative_displacement * entry_time - cur_face_normal * face_distance;

    // 3.1. projection point is inside triangle: retrun barycentric
    if (!compute_barycentric(projected_position, cur_triangle_pos, barycentric)) {
        return false;
    }
    if (is_inside_triangle(barycentric)) {
        return true;
    }

    // 3.2. projection point is outside triangle: check edge contacts
    bool check_ab = barycentric.z < 0.0;
    bool check_bc = barycentric.x < 0.0;
    bool check_ca = barycentric.y < 0.0;

    float earliest_contact_time = max_float;
    if (check_ab) {
        EdgePositions edge_ab = EdgePositions(cur_triangle_pos.a, cur_triangle_pos.b);
        EdgeContact contact = find_edge_contact(relative_prev_vertex_pos, relative_displacement, edge_ab, thickness);
        if (contact.time < earliest_contact_time) {
            earliest_contact_time = contact.time;
            barycentric = vec3(1.0 - contact.edge_fraction, contact.edge_fraction, 0.0);
            if (earliest_contact_time == 0.0) {
                return true;
            }
        }
    }
    if (check_bc) {
        EdgePositions edge_bc = EdgePositions(cur_triangle_pos.b, cur_triangle_pos.c);
        EdgeContact contact = find_edge_contact(relative_prev_vertex_pos, relative_displacement, edge_bc, thickness);
        if (contact.time < earliest_contact_time) {
            earliest_contact_time = contact.time;
            barycentric = vec3(0.0, 1.0 - contact.edge_fraction, contact.edge_fraction);
            if (earliest_contact_time == 0.0) {
                return true;
            }
        }
    }
    if (check_ca) {
        EdgePositions edge_ca = EdgePositions(cur_triangle_pos.c, cur_triangle_pos.a);
        EdgeContact contact = find_edge_contact(relative_prev_vertex_pos, relative_displacement, edge_ca, thickness);
        if (contact.time < earliest_contact_time) {
            earliest_contact_time = contact.time;
            barycentric = vec3(contact.edge_fraction, 0.0, 1.0 - contact.edge_fraction);
        }
    }

    if (has_contact(earliest_contact_time)) {
        return true;
    }

    // 4. check current position contact
    return find_current_contact_barycentric(cur_vertex_pos,
                                            cur_triangle_pos,
                                            cur_face_normal,
                                            cur_signed_distance,
                                            barycentric);
}

#endif
