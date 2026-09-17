#ifndef CLOTH_CONTACT_CORRECTION_GLSL
#define CLOTH_CONTACT_CORRECTION_GLSL

vec3 compute_vertex_motion_delta(uint vertex_index)
{
    vec3 position_delta = read_cloth_current_position(vertex_index) - read_cloth_previous_position(vertex_index);
    vec3 collision_delta = collision_pushouts[vertex_index].xyz + cloth_cloth_pushouts[vertex_index].xyz;
    return position_delta - collision_delta + contact_motion_deltas[vertex_index].xyz;
}

void accumulate_contact_correction(uint vertex_index,
                                   float vertex_weight,
                                   vec3 correction,
                                   vec3 contact_motion_delta)
{
    if (vertex_weight == 0.0) {
        return;
    }

    accumulate_vertex_normal_correction(vertex_index, vertex_weight * correction);
    atomicAdd(normal_correction_sums[vertex_index].w, 1);
    accumulate_vertex_contact_motion_delta(vertex_index, vertex_weight * contact_motion_delta);
}

#endif
