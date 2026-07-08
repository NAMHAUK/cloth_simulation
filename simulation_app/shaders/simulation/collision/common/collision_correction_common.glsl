#ifndef COLLISION_CORRECTION_COMMON_GLSL
#define COLLISION_CORRECTION_COMMON_GLSL

const float correction_fixed_point_scale = 1000000.0;
const float correction_component_limit = 1073741823.0;

#ifdef COLLISION_CORRECTION_ACCUMULATE
void accumulate_vertex_correction(uint vertex_index, vec3 correction)
{
    vec3 scaled_correction = round(correction * correction_fixed_point_scale);
    ivec3 encoded_correction = ivec3(clamp(scaled_correction,
                                           vec3(-correction_component_limit),
                                           vec3(correction_component_limit)));

    atomicAdd(correction_sums[vertex_index].x, encoded_correction.x);
    atomicAdd(correction_sums[vertex_index].y, encoded_correction.y);
    atomicAdd(correction_sums[vertex_index].z, encoded_correction.z);
}
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

void apply_position_correction(uint vertex_index, vec3 correction)
{
    vec3 corrected_position = read_cloth_current_position(vertex_index) + correction;
    write_cloth_current_position(vertex_index, corrected_position);

    vec4 collision_state = collision_states[vertex_index];
    collision_state.xyz += correction;
    collision_states[vertex_index] = collision_state;
}
#endif

#endif
