#ifndef CLOTH_CONTACT_CORRECTION_GLSL
#define CLOTH_CONTACT_CORRECTION_GLSL

#define POSITION_IO_CLOTH_CURRENT
#define POSITION_IO_CLOTH_PREVIOUS
#define COLLISION_CORRECTION_ACCUMULATE

#include "../../mesh/position_io.glsl"
#include "../contact_geometry.glsl"
#include "../correction.glsl"

#ifdef COLLISION_INWARD_MOTION_ACCUMULATE
vec3 compute_vertex_motion_delta(uint vertex_index)
{
    vec3 position_delta = read_cloth_current_position(vertex_index) - read_cloth_previous_position(vertex_index);
    vec3 collision_delta = collision_pushouts[vertex_index].xyz;
    return position_delta - collision_delta + inward_motion_corrections[vertex_index].xyz;
}
#endif

bool accumulate_vertex_face_penetration_correction(uint vertex_index,
                                                   uvec3 face_vertex_indices,
                                                   out vec3 normal,
                                                   out vec3 barycentric,
                                                   out float inverse_denominator)
{   
    // read
    vec3 prev_vertex_pos = read_cloth_previous_position(vertex_index);
    vec3 cur_vertex_pos = read_cloth_current_position(vertex_index);
    TrianglePositions prev_triangle_pos = read_cloth_previous_triangle(face_vertex_indices);
    TrianglePositions cur_triangle_pos = read_cloth_current_triangle(face_vertex_indices);

    vec4 previous_normal = triangle_normal(prev_triangle_pos.a, prev_triangle_pos.b, prev_triangle_pos.c);
    vec4 current_normal = triangle_normal(cur_triangle_pos.a, cur_triangle_pos.b, cur_triangle_pos.c);
    if (previous_normal.w == 0.0 || current_normal.w == 0.0) {
        return false;
    }

    float prev_distance = dot(prev_vertex_pos - prev_triangle_pos.a, previous_normal.xyz);
    float cur_distance = dot(cur_vertex_pos - cur_triangle_pos.a, current_normal.xyz);
    float signed_distance = abs(prev_distance) <= 1.0e-8 ? cur_distance : prev_distance;
    float normal_sign = signed_distance < 0.0 ? -1.0 : 1.0;

    prev_distance *= normal_sign;
    cur_distance *= normal_sign;
    current_normal.xyz *= normal_sign;

    // check contact
    bool has_swept_contact = false;
    float distance_delta = prev_distance - cur_distance;
    float contact_distance = prev_distance > uCollisionThickness ? uCollisionThickness : 0.0;

    if (cur_distance <= contact_distance && distance_delta > 1.0e-8) {
        float contact_time = clamp((prev_distance - contact_distance) / distance_delta, 0.0, 1.0);
        vec3 contact_vertex_pos = mix(prev_vertex_pos, cur_vertex_pos, contact_time);
        TrianglePositions contact_face = interpolate_triangle_positions(prev_triangle_pos, cur_triangle_pos, contact_time);

        has_swept_contact = compute_barycentric(contact_vertex_pos, contact_face, barycentric) &&
                            is_inside_triangle(barycentric);
    }

    // compute penetration depth
    float penetration_depth;
    if (has_swept_contact) {
        normal = current_normal.xyz;
        penetration_depth = uCollisionThickness - cur_distance;
        if (penetration_depth <= 0.0) {
            return false;
        }
    } else {
        vec3 closest_point = closest_point_on_triangle(cur_vertex_pos, cur_triangle_pos);
        vec3 separation = cur_vertex_pos - closest_point;
        float distance = length(separation);
        if (distance >= uCollisionThickness ||
            !compute_barycentric(closest_point, cur_triangle_pos, barycentric) ||
            !is_inside_triangle(barycentric)) {
            return false;
        }

        normal = distance > 0.0 ? separation / distance : current_normal.xyz;
        penetration_depth = uCollisionThickness - distance;
    }

    // accumulate collision correction
    inverse_denominator = 1.0 / (1.0 + dot(barycentric, barycentric));
    float correction_distance = penetration_depth * uCollisionStiffness;
    uvec4 vertex_indices = uvec4(vertex_index, face_vertex_indices);
    vec4 weights = vec4(1.0, -barycentric) * inverse_denominator;
    
    for (uint i = 0u; i < 4u; ++i) {
        if (weights[i] != 0.0) {
            accumulate_vertex_normal_correction(vertex_indices[i], normal, correction_distance * weights[i]);
        }
    }
    return true;
}

#ifdef COLLISION_INWARD_MOTION_ACCUMULATE
void accumulate_vertex_face_collision_correction(uint vertex_index, uvec3 face_vertex_indices)
{
    vec3 normal;
    vec3 barycentric;
    float inverse_denominator;
    if (!accumulate_vertex_face_penetration_correction(vertex_index,
                                                       face_vertex_indices,
                                                       normal,
                                                       barycentric,
                                                       inverse_denominator)) {
        return;
    }

    vec3 face_delta = compute_vertex_motion_delta(face_vertex_indices.x) * barycentric.x +
                      compute_vertex_motion_delta(face_vertex_indices.y) * barycentric.y +
                      compute_vertex_motion_delta(face_vertex_indices.z) * barycentric.z;
    vec3 relative_delta = compute_vertex_motion_delta(vertex_index) - face_delta;
    uvec4 vertex_indices = uvec4(vertex_index, face_vertex_indices);
    vec4 weights = vec4(1.0, -barycentric) * inverse_denominator;
    for (uint i = 0u; i < 4u; ++i) {
        accumulate_vertex_inward_motion_correction(vertex_indices[i], normal, relative_delta, weights[i]);
    }
}

void accumulate_edge_edge_collision_correction(uvec4 vertex_indices)
{   
    // read & calculate edge closest point 
    EdgePositions cur_edge1_pos = read_cloth_current_edge(vertex_indices.xy);
    EdgePositions cur_edge2_pos = read_cloth_current_edge(vertex_indices.zw);
    SegmentState cur_segment1 = SegmentState(cur_edge1_pos, 0.0, cur_edge1_pos.a);
    SegmentState cur_segment2 = SegmentState(cur_edge2_pos, 0.0, cur_edge2_pos.a);
    if (!closest_segment_points(cur_segment1, cur_segment2)) {
        return;
    }

    if (cur_segment1.t <= 0.0 || cur_segment1.t >= 1.0 || cur_segment2.t <= 0.0 || cur_segment2.t >= 1.0) {
        return;
    }

    vec3 current_delta = cur_segment1.point - cur_segment2.point;
    float current_distance_sq = dot(current_delta, current_delta);
    if (current_distance_sq >= uCollisionThickness * uCollisionThickness) {
        return;
    }

    EdgePositions prev_edge1_pos = read_cloth_previous_edge(vertex_indices.xy);
    EdgePositions prev_edge2_pos = read_cloth_previous_edge(vertex_indices.zw);
    SegmentState prev_segment1 = SegmentState(prev_edge1_pos, 0.0, prev_edge1_pos.a);
    SegmentState prev_segment2 = SegmentState(prev_edge2_pos, 0.0, prev_edge2_pos.a);
    if (!closest_segment_points(prev_segment1, prev_segment2)) {
        return;
    }

    vec3 previous_delta = prev_segment1.point - prev_segment2.point;
    float previous_distance_sq = dot(previous_delta, previous_delta);
    if (previous_distance_sq <= 1.0e-8) {
        return;
    }

    // accumulate collision correction
    vec3 normal = previous_delta * inversesqrt(previous_distance_sq);
    vec4 weights = vec4(1.0 - cur_segment1.t, cur_segment1.t, cur_segment2.t - 1.0, -cur_segment2.t);
    vec3 relative_delta = vec3(0.0);
    for (uint i = 0u; i < 4u; ++i) {
        relative_delta += weights[i] * compute_vertex_motion_delta(vertex_indices[i]);
    }

    float penetration_depth = uCollisionThickness - dot(current_delta, normal);
    float correction_distance = penetration_depth * uCollisionStiffness;
    weights /= dot(weights, weights);
    for (uint i = 0u; i < 4u; ++i) {
        if (weights[i] != 0.0) {
            accumulate_vertex_normal_correction(vertex_indices[i], normal, correction_distance * weights[i]);
            accumulate_vertex_inward_motion_correction(vertex_indices[i], normal, relative_delta, weights[i]);
        }
    }
}
#endif

#ifdef CLOTH_INITIAL_LAYER_CONTACT
#include "../../bvh/body_surface_search.glsl"

void accumulate_initial_layer_collision_correction(uint vertex_index,
                                                   uvec3 face_vertex_indices,
                                                   bool is_outer_vertex)
{   
    // check triangle & vertex projection point
    vec3 vertex_position = read_cloth_current_position(vertex_index);
    TrianglePositions face = read_cloth_current_triangle(face_vertex_indices);

    vec4 face_normal = triangle_normal(face.a, face.b, face.c);
    if (face_normal.w == 0.0) {
        return;
    }

    vec3 barycentric;
    float signed_distance = dot(vertex_position - face.a, face_normal.xyz);
    vec3 surface_point = vertex_position - face_normal.xyz * signed_distance;
    if (!compute_barycentric(surface_point, face, barycentric) ||
        !is_inside_triangle(barycentric)) {
        return;
    }

    // align normal & compute penetration depth
    NearestBodySurface nearest_surface;
    if (find_nearest_body_surface(surface_point, uSearchRadiusSquared, nearest_surface) &&
        dot(face_normal.xyz, nearest_surface.normal) < 0.0) {
        face_normal.xyz = -face_normal.xyz;
        signed_distance = -signed_distance;
    }

    float layer_distance = is_outer_vertex ? signed_distance : -signed_distance;
    float penetration_depth = uCollisionThickness - layer_distance;
    if (penetration_depth <= 0.0) {
        return;
    }

    // accumulate outer garment correction
    float correction_distance = penetration_depth * uCollisionStiffness;
    uvec4 vertex_indices = uvec4(vertex_index, face_vertex_indices);
    vec4 weights = is_outer_vertex ? vec4(1.0, 0.0, 0.0, 0.0) : vec4(0.0, barycentric);
    weights *= 1.0 / dot(weights, weights);
    for (uint i = 0u; i < 4u; ++i) {
        if (weights[i] != 0.0) {
            accumulate_vertex_normal_correction(vertex_indices[i],
                                                face_normal.xyz,
                                                correction_distance * weights[i]);
        }
    }
}
#endif

#endif
