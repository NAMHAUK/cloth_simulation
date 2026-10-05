#ifndef CLOTH_BODY_CONTACT_CORRECTION_GLSL
#define CLOTH_BODY_CONTACT_CORRECTION_GLSL

#define POSITION_IO_CLOTH_CURRENT
#define POSITION_IO_CLOTH_PREVIOUS
#define POSITION_IO_BODY_PREVIOUS
#define COLLISION_CORRECTION_ACCUMULATE
#define COLLISION_INWARD_MOTION_ACCUMULATE
#define COLLISION_CORRECTION_FRICTION_ACCUMULATE

#ifdef CLOTH_BODY_VERTEX_FACE_CONTACT
#define POSITION_IO_TRIANGLE_INDICES
#endif

#ifdef CLOTH_BODY_EDGE_EDGE_CONTACT
#define POSITION_IO_BODY_CURRENT
#endif

#include "../../mesh/position_io.glsl"
#include "../contact_geometry.glsl"
#include "../correction.glsl"

#ifdef CLOTH_BODY_VERTEX_FACE_CONTACT
void accumulate_vertex_face_collision_correction(uint cloth_vertex_index, uint triangle_index)
{
    vec3 face_normal = body_triangle_normals[triangle_index].xyz;
    if (dot(face_normal, face_normal) <= 1.0e-20) {
        return;
    }

    TrianglePositions cur_triangle_pos = body_triangle_positions[triangle_index];
    TrianglePositions prev_triangle_pos = read_body_previous_triangle(read_triangle_indices(triangle_index));
    vec3 cur_vertex_pos = read_cloth_current_position(cloth_vertex_index);
    vec3 prev_vertex_pos = read_cloth_previous_position(cloth_vertex_index);

    vec3 delta_a = cur_triangle_pos.a - prev_triangle_pos.a;
    vec3 delta_b = cur_triangle_pos.b - prev_triangle_pos.b;
    vec3 delta_c = cur_triangle_pos.c - prev_triangle_pos.c;
    vec3 delta_triangle = (delta_a + delta_b + delta_c) / 3.0;
    vec3 relative_prev_vertex_pos = prev_vertex_pos + delta_triangle;

    float cur_signed_distance = dot(cur_vertex_pos - cur_triangle_pos.a, face_normal);
    float penetration_depth = uCollisionThickness - cur_signed_distance;
    if (penetration_depth <= 0.0) {
        return;
    }

    vec3 barycentric;
    if (!find_contact_barycentric(relative_prev_vertex_pos,
                                  cur_vertex_pos,
                                  cur_triangle_pos,
                                  face_normal,
                                  cur_signed_distance,
                                  uCollisionThickness,
                                  barycentric)) {
        return;
    }

    // collision correction calculation
    vec3 triangle_delta = delta_a * barycentric.x + delta_b * barycentric.y + delta_c * barycentric.z;
    vec3 vertex_delta = cur_vertex_pos - prev_vertex_pos - collision_pushouts[cloth_vertex_index].xyz;

    vec3 relative_delta = vertex_delta - triangle_delta;
    vec3 friction_correction = compute_friction_correction(face_normal, relative_delta, penetration_depth);
    vec3 inward_corrected_relative_delta = relative_delta + inward_motion_corrections[cloth_vertex_index].xyz;

    // collision correction accumulation
    accumulate_vertex_normal_correction(cloth_vertex_index, face_normal, penetration_depth);
    accumulate_vertex_friction_correction(cloth_vertex_index, friction_correction);
    accumulate_vertex_inward_motion_correction(cloth_vertex_index,
                                               face_normal,
                                               inward_corrected_relative_delta,
                                               1.0);
}
#endif

#ifdef CLOTH_BODY_EDGE_EDGE_CONTACT
void accumulate_edge_edge_collision_correction(uvec2 candidate)
{
    // read & calculate edge closest point
    uvec2 cloth_vertices = uvec2(cloth_edge_indices[candidate.x * 2u],
                                 cloth_edge_indices[candidate.x * 2u + 1u]);
    uvec2 body_vertices = uvec2(body_edge_indices[candidate.y * 2u],
                                body_edge_indices[candidate.y * 2u + 1u]);

    EdgePositions cur_cloth_edge_pos = read_cloth_current_edge(cloth_vertices);
    EdgePositions cur_body_edge_pos = read_body_current_edge(body_vertices);
    SegmentState cur_cloth_segment = SegmentState(cur_cloth_edge_pos, 0.0, cur_cloth_edge_pos.a);
    SegmentState cur_body_segment = SegmentState(cur_body_edge_pos, 0.0, cur_body_edge_pos.a);
    if (!closest_segment_points(cur_cloth_segment, cur_body_segment)) {
        return;
    }

    vec3 current_delta = cur_cloth_segment.point - cur_body_segment.point;
    float current_distance_sq = dot(current_delta, current_delta);
    if (current_distance_sq >= uCollisionThickness * uCollisionThickness) {
        return;
    }

    EdgePositions prev_cloth_edge_pos = read_cloth_previous_edge(cloth_vertices);
    EdgePositions prev_body_edge_pos = read_body_previous_edge(body_vertices);
    SegmentState prev_cloth_segment = SegmentState(prev_cloth_edge_pos, 0.0, prev_cloth_edge_pos.a);
    SegmentState prev_body_segment = SegmentState(prev_body_edge_pos, 0.0, prev_body_edge_pos.a);
    if (!closest_segment_points(prev_cloth_segment, prev_body_segment)) {
        return;
    }

    vec3 previous_delta = prev_cloth_segment.point - prev_body_segment.point;
    float previous_distance_sq = dot(previous_delta, previous_delta);
    if (previous_distance_sq <= 1.0e-8) {
        return;
    }

    // accumulate collision correction
    vec3 normal = previous_delta * inversesqrt(previous_distance_sq);
    float penetration_depth = uCollisionThickness - dot(current_delta, normal);
    vec3 inward_motion_correction = mix(inward_motion_corrections[cloth_vertices.x].xyz,
                                        inward_motion_corrections[cloth_vertices.y].xyz,
                                        cur_cloth_segment.t);
    vec3 collision_pushout = mix(collision_pushouts[cloth_vertices.x].xyz,
                                 collision_pushouts[cloth_vertices.y].xyz,
                                 cur_cloth_segment.t);
    vec3 cloth_delta = cur_cloth_segment.point - interpolate_edge_position(prev_cloth_edge_pos, cur_cloth_segment.t) - collision_pushout;
    vec3 body_delta = cur_body_segment.point - interpolate_edge_position(prev_body_edge_pos, cur_body_segment.t);
    vec3 relative_delta = cloth_delta - body_delta;
    vec3 friction_correction = compute_friction_correction(normal, relative_delta, penetration_depth);

    vec2 weights = vec2(1.0 - cur_cloth_segment.t, cur_cloth_segment.t);
    weights /= dot(weights, weights);

    for (uint i = 0u; i < 2u; ++i) {
        if (weights[i] > 0.0) {
            accumulate_vertex_normal_correction(cloth_vertices[i], normal, penetration_depth * weights[i]);
            accumulate_vertex_friction_correction(cloth_vertices[i], friction_correction * weights[i]);
            accumulate_vertex_inward_motion_correction(cloth_vertices[i],
                                                       normal,
                                                       relative_delta + inward_motion_correction,
                                                       weights[i]);
        }
    }
}
#endif

#endif
