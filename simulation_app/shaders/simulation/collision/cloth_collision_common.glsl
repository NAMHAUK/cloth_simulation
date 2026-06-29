struct Aabb {
    vec3 min_bounds;
    vec3 max_bounds;
};

Aabb build_swept_aabb(vec3 previous_position, vec3 current_position)
{
    Aabb result;
    result.min_bounds = min(previous_position, current_position);
    result.max_bounds = max(previous_position, current_position);
    return result;
}

bool overlaps_bounds(Aabb query_bounds, vec4 min_bounds, vec4 max_bounds)
{
    return all(lessThanEqual(query_bounds.min_bounds, max_bounds.xyz)) &&
           all(greaterThanEqual(query_bounds.max_bounds, min_bounds.xyz));
}

void store_contact(uint vertex_index, vec3 normal, float weight)
{
    vec4 collision_state = collision_states[vertex_index];
    uint stored_count = min(uint(collision_state.w + 0.5), uMaxContactsPerVertex);
    uint contact_base = vertex_index * uMaxContactsPerVertex;
    if (stored_count < uMaxContactsPerVertex) {
        contact_normals[contact_base + stored_count] = vec4(normal, weight);
        collision_state.w = float(stored_count + 1u);
        collision_states[vertex_index] = collision_state;
        return;
    }

    uint weakest_index = 0u;
    float weakest_weight = contact_normals[contact_base].w;
    for (uint contact_index = 1u; contact_index < uMaxContactsPerVertex; ++contact_index) {
        float current_weight = contact_normals[contact_base + contact_index].w;
        if (current_weight < weakest_weight) {
            weakest_index = contact_index;
            weakest_weight = current_weight;
        }
    }

    if (weight > weakest_weight) {
        contact_normals[contact_base + weakest_index] = vec4(normal, weight);
    }
}
