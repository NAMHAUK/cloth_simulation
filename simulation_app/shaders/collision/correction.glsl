#ifndef COLLISION_CORRECTION_COMMON_GLSL
#define COLLISION_CORRECTION_COMMON_GLSL

const float int_correction_scale = 1000000.0;
const float int_correction_limit = 1073741823.0;

#ifdef COLLISION_CORRECTION_ACCUMULATE
ivec3 correction_to_int(vec3 correction)
{
    vec3 scaled_correction = round(correction * int_correction_scale);
    return ivec3(clamp(scaled_correction, vec3(-int_correction_limit), vec3(int_correction_limit)));
}

void accumulate_vertex_normal_correction(uint vertex_index, vec3 normal, float correction_distance)
{
    vec3 correction = normal * correction_distance;
    ivec3 int_correction = correction_to_int(correction);
    atomicAdd(normal_correction_sums[vertex_index].x, int_correction.x);
    atomicAdd(normal_correction_sums[vertex_index].y, int_correction.y);
    atomicAdd(normal_correction_sums[vertex_index].z, int_correction.z);
    atomicAdd(normal_correction_sums[vertex_index].w, 1);
}

#ifdef COLLISION_INWARD_MOTION_ACCUMULATE
void accumulate_vertex_inward_motion_correction(uint vertex_index,
                                                vec3 normal,
                                                vec3 relative_delta,
                                                float weight)
{
    vec3 correction = normal * max(-dot(relative_delta, normal), 0.0) * weight;
    ivec3 int_correction = correction_to_int(correction);
    if (all(equal(int_correction, ivec3(0)))) {
        return;
    }

    atomicAdd(inward_motion_correction_sums[vertex_index].x, int_correction.x);
    atomicAdd(inward_motion_correction_sums[vertex_index].y, int_correction.y);
    atomicAdd(inward_motion_correction_sums[vertex_index].z, int_correction.z);
    atomicAdd(inward_motion_correction_sums[vertex_index].w, 1);
}
#endif

#ifdef COLLISION_CORRECTION_FRICTION_ACCUMULATE
vec3 compute_friction_correction(vec3 normal, vec3 relative_delta, float penetration_depth)
{
    vec3 correction = -(relative_delta - normal * dot(relative_delta, normal));
    float friction_length = length(correction);
    if (penetration_depth <= 1.0e-8 || friction_length <= 1.0e-8) {
        return vec3(0.0);
    } else if (friction_length <= uStaticFriction * penetration_depth) {
        return correction;
    } else {
        return correction * (uDynamicFriction * penetration_depth / friction_length);
    }
}

void accumulate_vertex_friction_correction(uint vertex_index, vec3 correction)
{
    ivec3 int_correction = correction_to_int(correction);
    atomicAdd(friction_correction_sums[vertex_index].x, int_correction.x);
    atomicAdd(friction_correction_sums[vertex_index].y, int_correction.y);
    atomicAdd(friction_correction_sums[vertex_index].z, int_correction.z);
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

void apply_position_correction(uint vertex_index, vec3 normal_correction, vec3 friction_correction)
{
    vec3 corrected_position = read_cloth_current_position(vertex_index) + normal_correction + friction_correction;
    write_cloth_current_position(vertex_index, corrected_position);

    collision_pushouts[vertex_index].xyz += normal_correction;
}
#endif

#endif
