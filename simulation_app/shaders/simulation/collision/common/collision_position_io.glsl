#ifndef COLLISION_POSITION_IO_GLSL
#define COLLISION_POSITION_IO_GLSL

#if defined(COLLISION_POSITION_IO_CLOTH_SWEEP) || defined(COLLISION_POSITION_IO_CLOTH_BODY_SWEEP) || defined(COLLISION_POSITION_IO_CLOTH_APPLY)
vec3 read_cloth_current_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(cloth_current_positions[base_index],
                cloth_current_positions[base_index + 1u],
                cloth_current_positions[base_index + 2u]);
}
#endif

#if defined(COLLISION_POSITION_IO_CLOTH_SWEEP) || defined(COLLISION_POSITION_IO_CLOTH_BODY_SWEEP)
vec3 read_cloth_previous_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(cloth_previous_positions[base_index],
                cloth_previous_positions[base_index + 1u],
                cloth_previous_positions[base_index + 2u]);
}
#endif

#ifdef COLLISION_POSITION_IO_CLOTH_BODY_SWEEP
vec3 read_body_current_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(body_current_positions[base_index],
                body_current_positions[base_index + 1u],
                body_current_positions[base_index + 2u]);
}
#endif

#ifdef COLLISION_POSITION_IO_CLOTH_BODY_SWEEP
vec3 read_body_previous_position(uint vertex_index)
{
    uint base_index = vertex_index * 3u;
    return vec3(body_previous_positions[base_index],
                body_previous_positions[base_index + 1u],
                body_previous_positions[base_index + 2u]);
}
#endif

#ifdef COLLISION_POSITION_IO_CLOTH_APPLY
void write_cloth_current_position(uint vertex_index, vec3 position)
{
    uint base_index = vertex_index * 3u;
    cloth_current_positions[base_index] = position.x;
    cloth_current_positions[base_index + 1u] = position.y;
    cloth_current_positions[base_index + 2u] = position.z;
}
#endif

#endif
