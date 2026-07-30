#ifndef COLLISION_CORRECTION_COMMON_GLSL
#define COLLISION_CORRECTION_COMMON_GLSL

const float correction_fixed_point_scale = 1000000.0;
const float correction_component_limit = 1073741823.0;

#ifdef COLLISION_CORRECTION_ACCUMULATE
ivec3 encode_correction(vec3 correction)
{
    vec3 scaled_correction = round(correction * correction_fixed_point_scale);
    return ivec3(clamp(scaled_correction,
                       vec3(-correction_component_limit),
                       vec3(correction_component_limit)));
}

void accumulate_vertex_normal_correction(uint vertex_index, vec3 correction)
{
    ivec3 encoded_correction = encode_correction(correction);
    atomicAdd(normal_correction_sums[vertex_index].x, encoded_correction.x);
    atomicAdd(normal_correction_sums[vertex_index].y, encoded_correction.y);
    atomicAdd(normal_correction_sums[vertex_index].z, encoded_correction.z);
}

#ifdef COLLISION_CONTACT_MOTION_ACCUMULATE
vec3 compute_required_contact_motion_delta(vec3 normal,
                                           vec3 cloth_delta,
                                           vec3 body_delta,
                                           vec3 retained_delta)
{
    vec3 relative_delta = cloth_delta + retained_delta - body_delta;
    float inward_delta = dot(relative_delta, normal);
    return normal * max(-inward_delta, 0.0);
}

void accumulate_vertex_contact_motion_delta(uint vertex_index, vec3 contact_motion_delta)
{
    ivec3 encoded_delta = encode_correction(contact_motion_delta);
    if (all(equal(encoded_delta, ivec3(0)))) {
        return;
    }

    atomicAdd(contact_motion_delta_sums[vertex_index].x, encoded_delta.x);
    atomicAdd(contact_motion_delta_sums[vertex_index].y, encoded_delta.y);
    atomicAdd(contact_motion_delta_sums[vertex_index].z, encoded_delta.z);
    atomicAdd(contact_motion_delta_sums[vertex_index].w, 1);
}
#endif

#ifdef COLLISION_CORRECTION_FRICTION_ACCUMULATE
vec3 compute_friction_correction(vec3 normal, vec3 cloth_delta, vec3 body_delta)
{
    vec3 relative_movement = cloth_delta - body_delta;
    vec3 tangent_movement = relative_movement - normal * dot(relative_movement, normal);
    return -tangent_movement;
}

void accumulate_vertex_friction_correction(uint vertex_index, vec3 friction_correction)
{
    ivec3 encoded_correction = encode_correction(friction_correction);
    atomicAdd(friction_correction_sums[vertex_index].x, encoded_correction.x);
    atomicAdd(friction_correction_sums[vertex_index].y, encoded_correction.y);
    atomicAdd(friction_correction_sums[vertex_index].z, encoded_correction.z);
}
#endif
#endif

#ifdef COLLISION_CORRECTION_APPLY
vec3 clamp_correction(vec3 correction)
{
    float correction_length_sq = dot(correction, correction);
    float max_correction_length_sq = uMaxCorrectionLength * uMaxCorrectionLength;
    if (correction_length_sq > max_correction_length_sq) {
        return correction * (uMaxCorrectionLength * inversesqrt(correction_length_sq));
    }

    return correction;
}

#ifdef COLLISION_CORRECTION_FRICTION_APPLY
const float friction_epsilon = 1.0e-8;

vec3 clamp_friction_correction(vec3 normal_correction, vec3 friction_correction)
{
    float normal_length_sq = dot(normal_correction, normal_correction);
    float friction_length_sq = dot(friction_correction, friction_correction);
    if (normal_length_sq <= friction_epsilon * friction_epsilon ||
        friction_length_sq <= friction_epsilon * friction_epsilon) {
        return vec3(0.0);
    }

    float normal_length = sqrt(normal_length_sq);
    float friction_length = sqrt(friction_length_sq);
    float static_limit = uStaticFriction * normal_length;
    if (friction_length <= static_limit) {
        return friction_correction;
    }

    float dynamic_limit = uDynamicFriction * normal_length;
    return friction_correction * (dynamic_limit / friction_length);
}
#endif

void apply_position_correction(uint vertex_index, vec3 normal_correction, vec3 friction_correction)
{
    vec3 corrected_position = read_cloth_current_position(vertex_index) + normal_correction + friction_correction;
    write_cloth_current_position(vertex_index, corrected_position);

    collision_pushouts[vertex_index].xyz += normal_correction;
}
#endif

#endif
