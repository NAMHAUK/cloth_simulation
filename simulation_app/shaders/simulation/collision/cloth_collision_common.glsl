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
