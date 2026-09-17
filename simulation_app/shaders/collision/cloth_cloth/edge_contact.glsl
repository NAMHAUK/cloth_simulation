#ifndef CLOTH_EDGE_CONTACT_GLSL
#define CLOTH_EDGE_CONTACT_GLSL

struct ClothEdgeEdgeContact {
    vec3 normal;
    vec4 weights;
    float depth;
};

bool closest_edges_at_time(EdgePositions previous_first,
                            EdgePositions previous_second,
                            EdgePositions current_first,
                            EdgePositions current_second,
                            float time,
                            out SegmentState first,
                            out SegmentState second)
{
    first.positions = EdgePositions(mix(previous_first.a, current_first.a, time),
                                   mix(previous_first.b, current_first.b, time));
    second.positions = EdgePositions(mix(previous_second.a, current_second.a, time),
                                    mix(previous_second.b, current_second.b, time));
    return closest_segment_points(first, second);
}

bool compute_cloth_edge_edge_contact(uvec4 vertices,
                                      float thickness,
                                      out ClothEdgeEdgeContact contact)
{
    EdgePositions previous_first = read_cloth_previous_edge(vertices.xy);
    EdgePositions previous_second = read_cloth_previous_edge(vertices.zw);
    EdgePositions current_first = read_cloth_current_edge(vertices.xy);
    EdgePositions current_second = read_cloth_current_edge(vertices.zw);
    SegmentState first;
    SegmentState second;
    if (!closest_edges_at_time(previous_first, previous_second, current_first, current_second, 0.0, first, second)) {
        return false;
    }

    vec3 previous_delta = first.point - second.point;
    float previous_distance = length(previous_delta);
    vec3 motion_a = current_first.a - previous_first.a;
    vec3 motion_b = current_first.b - previous_first.b;
    vec3 motion_c = current_second.a - previous_second.a;
    vec3 motion_d = current_second.b - previous_second.b;
    vec3 average_motion = (motion_a + motion_b + motion_c + motion_d) * 0.25;
    float speed_bound = max(length(motion_a - average_motion), length(motion_b - average_motion))
                      + max(length(motion_c - average_motion), length(motion_d - average_motion));
    float tolerance = max(thickness * 1.0e-4, 1.0e-7);
    // Inside the thickness shell, preserve the starting side by approaching a smaller gap.
    float target_distance = previous_distance > thickness ? thickness : previous_distance * 0.1;
    float time = 0.0;
    float distance = previous_distance;
    bool has_swept_contact = false;
    if (previous_distance > tolerance && speed_bound > tolerance) {
        for (uint iteration = 0u; iteration < 64u; ++iteration) {
            float gap = distance - target_distance;
            if (gap <= tolerance) {
                has_swept_contact = true;
                break;
            }
            float time_step = 0.9 * gap / speed_bound;
            if (time_step > 1.0 - time) {
                break;
            }
            float next_time = time + time_step;
            if (next_time == time || iteration == 63u) {
                has_swept_contact = true;
                break;
            }
            time = next_time;
            if (!closest_edges_at_time(previous_first, previous_second, current_first, current_second, time, first, second)) {
                return false;
            }
            distance = length(first.point - second.point);
        }
    }

    if (!has_swept_contact) {
        if (!closest_edges_at_time(previous_first, previous_second, current_first, current_second, 1.0, first, second)) {
            return false;
        }
        distance = length(first.point - second.point);
        if (distance >= thickness) {
            return false;
        }
        // VF already handles discrete vertex-edge and vertex-vertex proximity.
        if (first.t <= 0.0 || first.t >= 1.0 || second.t <= 0.0 || second.t >= 1.0) {
            return false;
        }
    }

    vec3 separation = first.point - second.point;
    if (distance > tolerance) {
        contact.normal = separation / distance;
    } else if (previous_distance > tolerance) {
        contact.normal = previous_delta / previous_distance;
    } else {
        vec3 normal = cross(first.positions.b - first.positions.a, second.positions.b - second.positions.a);
        float normal_length = length(normal);
        if (normal_length <= 0.0) {
            return false;
        }
        contact.normal = normal / normal_length;
    }
    contact.weights = vec4(1.0 - first.t, first.t, second.t - 1.0, -second.t);
    vec3 current_delta = interpolate_edge_position(current_first, first.t) - interpolate_edge_position(current_second, second.t);
    contact.depth = thickness - dot(current_delta, contact.normal);
    return contact.depth > 0.0;
}

#endif
