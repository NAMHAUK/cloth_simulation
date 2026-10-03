#ifndef CLOTH_CONTACT_CORRECTION_GLSL
#define CLOTH_CONTACT_CORRECTION_GLSL

#define COLLISION_CORRECTION_ACCUMULATE
#include "../correction.glsl"

void accumulate_contact_correction(uint vertex_index, float vertex_weight, vec3 correction)
{
    if (vertex_weight == 0.0) {
        return;
    }

    accumulate_vertex_normal_correction(vertex_index, vertex_weight * correction);
    atomicAdd(normal_correction_sums[vertex_index].w, 1);
}

#ifdef COLLISION_INWARD_MOTION_ACCUMULATE
vec3 compute_vertex_motion_delta(uint vertex_index)
{
    vec3 position_delta = read_cloth_current_position(vertex_index) - read_cloth_previous_position(vertex_index);
    vec3 collision_delta = collision_pushouts[vertex_index].xyz;
    return position_delta - collision_delta + inward_motion_corrections[vertex_index].xyz;
}

void accumulate_contact_correction(uint vertex_index,
                                   float vertex_weight,
                                   vec3 correction,
                                   vec3 inward_motion_correction)
{
    accumulate_contact_correction(vertex_index, vertex_weight, correction);
    accumulate_vertex_inward_motion_correction(vertex_index, vertex_weight * inward_motion_correction);
}

void accumulate_edge_edge_correction(uvec4 vertices, ClothEdgeEdgeContact contact)
{
    float inverse_denominator = 1.0 / dot(contact.weights, contact.weights);
    vec3 correction = contact.normal * contact.depth * uCollisionStiffness * inverse_denominator;
    vec3 relative_delta = vec3(0.0);
    for (uint corner = 0u; corner < 4u; ++corner) {
        relative_delta += contact.weights[corner] * compute_vertex_motion_delta(vertices[corner]);
    }
    vec3 inward_motion_correction = contact.normal * max(-dot(relative_delta, contact.normal), 0.0) * inverse_denominator;
    for (uint corner = 0u; corner < 4u; ++corner) {
        accumulate_contact_correction(vertices[corner],
                                      contact.weights[corner],
                                      correction,
                                      inward_motion_correction);
    }
}

void accumulate_vertex_face_correction(uint vertex_index, uvec3 face_vertices, ClothVertexFaceContact contact)
{
    float inverse_denominator = 1.0 / (1.0 + dot(contact.barycentric, contact.barycentric));
    vec3 correction = contact.correction_normal * contact.depth * uCollisionStiffness * inverse_denominator;
    vec3 face_delta = compute_vertex_motion_delta(face_vertices.x) * contact.barycentric.x +
                      compute_vertex_motion_delta(face_vertices.y) * contact.barycentric.y +
                      compute_vertex_motion_delta(face_vertices.z) * contact.barycentric.z;
    vec3 relative_delta = compute_vertex_motion_delta(vertex_index) - face_delta;
    float inward_delta = dot(relative_delta, contact.correction_normal);
    vec3 inward_motion_correction = contact.correction_normal * max(-inward_delta, 0.0) * inverse_denominator;

    accumulate_contact_correction(vertex_index, 1.0, correction, inward_motion_correction);
    accumulate_contact_correction(face_vertices.x,
                                  -contact.barycentric.x,
                                  correction,
                                  inward_motion_correction);
    accumulate_contact_correction(face_vertices.y,
                                  -contact.barycentric.y,
                                  correction,
                                  inward_motion_correction);
    accumulate_contact_correction(face_vertices.z,
                                  -contact.barycentric.z,
                                  correction,
                                  inward_motion_correction);
}
#endif

#ifdef CLOTH_INITIAL_LAYER_CONTACT
void initial_accumulate_vertex_face_correction(uint vertex_index,
                                               uvec3 face_vertices,
                                               ClothVertexFaceContact contact)
{
    float inverse_denominator = 1.0 / (1.0 + dot(contact.barycentric, contact.barycentric));
    vec3 correction = contact.correction_normal * contact.depth * uCollisionStiffness * inverse_denominator;

    accumulate_contact_correction(vertex_index, 1.0, correction);
    accumulate_contact_correction(face_vertices.x, -contact.barycentric.x, correction);
    accumulate_contact_correction(face_vertices.y, -contact.barycentric.y, correction);
    accumulate_contact_correction(face_vertices.z, -contact.barycentric.z, correction);
}

void accumulate_initial_layer_correction(uint vertex_index,
                                         uvec3 face_vertices,
                                         bool is_upper_vertex,
                                         ClothVertexFaceContact contact)
{
    vec3 correction = contact.correction_normal * contact.depth * uCollisionStiffness;

    if (is_upper_vertex) {
        accumulate_contact_correction(vertex_index, 1.0, correction);
    } else {
        float inverse_denominator = 1.0 / dot(contact.barycentric, contact.barycentric);
        vec3 weights = max(contact.barycentric, vec3(0.0)) * inverse_denominator;
        accumulate_contact_correction(face_vertices.x, weights.x, correction);
        accumulate_contact_correction(face_vertices.y, weights.y, correction);
        accumulate_contact_correction(face_vertices.z, weights.z, correction);
    }
}
#endif

#endif
