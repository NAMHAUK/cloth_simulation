#ifndef CLOTH_EDGE_CONTACT_GLSL
#define CLOTH_EDGE_CONTACT_GLSL

struct ClothEdgeEdgeContact {
    vec3 normal;
    vec4 weights;
    float depth;
};

bool compute_cloth_edge_edge_contact(uvec2 first_edge,
                                      uvec2 second_edge,
                                      float thickness,
                                      out ClothEdgeEdgeContact contact)
{
    EdgePositions current_first = read_cloth_current_edge(first_edge);
    EdgePositions current_second = read_cloth_current_edge(second_edge);
    SegmentState first = SegmentState(current_first, 0.0, current_first.a);
    SegmentState second = SegmentState(current_second, 0.0, current_second.a);
    if (!closest_segment_points(first, second)) {
        return false;
    }

    vec3 current_delta = first.point - second.point;
    float contact_distance = thickness - penetration_tolerance;
    float current_distance_sq = dot(current_delta, current_delta);
    if (contact_distance <= 0.0 ||
        current_distance_sq >= contact_distance * contact_distance) {
        return false;
    }
    
    if (first.t <= 0.0 || first.t >= 1.0 || second.t <= 0.0 || second.t >= 1.0) {
        return false;
    }

    EdgePositions previous_first = read_cloth_previous_edge(first_edge);
    EdgePositions previous_second = read_cloth_previous_edge(second_edge);
    SegmentState previous_first_segment = SegmentState(previous_first, 0.0, previous_first.a);
    SegmentState previous_second_segment = SegmentState(previous_second, 0.0, previous_second.a);
    if (!closest_segment_points(previous_first_segment, previous_second_segment)) {
        return false;
    }

    vec3 previous_delta = previous_first_segment.point - previous_second_segment.point;
    float previous_distance_sq = dot(previous_delta, previous_delta);
    const float normal_distance_sq_epsilon = 1.0e-8;
    if (current_distance_sq <= normal_distance_sq_epsilon) {
        if (previous_distance_sq <= normal_distance_sq_epsilon) {
            return false;
        }
        contact.normal = previous_delta * inversesqrt(previous_distance_sq);
    } else {
        contact.normal = current_delta * inversesqrt(current_distance_sq);
    }

    if (previous_distance_sq > thickness * thickness &&
        dot(previous_delta, contact.normal) <= thickness) {
        return false;
    }

    contact.weights = vec4(1.0 - first.t, first.t, second.t - 1.0, -second.t);
    contact.depth = thickness - dot(current_delta, contact.normal) - penetration_tolerance;
    return contact.depth > 0.0;
}

#endif
