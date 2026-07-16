#ifndef BVH_COMMON_GLSL
#define BVH_COMMON_GLSL

struct BvhNode {
    vec4 min_bounds;
    vec4 max_bounds;
    uint left_child_index;
    uint right_child_index;
    uint first_element_index;
    uint element_count;
};

struct Aabb {
    vec4 min_bounds;
    vec4 max_bounds;
};

bool is_leaf_node(BvhNode node)
{
    return node.element_count > 0u;
}

bool is_ignored_body_part_node(BvhNode node, uint ignored_body_part_mask)
{
    uint part_label_mask = uint(node.max_bounds.w + 0.5);
    return part_label_mask != 0u && (part_label_mask & ~ignored_body_part_mask) == 0u;
}

Aabb build_swept_aabb(vec3 previous_position, vec3 current_position)
{
    return Aabb(vec4(min(previous_position, current_position), 0.0),
                vec4(max(previous_position, current_position), 0.0));
}

Aabb expand_aabb(Aabb bounds, vec3 expansion)
{
    vec4 expansion4 = vec4(expansion, 0.0);
    return Aabb(bounds.min_bounds - expansion4,
                bounds.max_bounds + expansion4);
}

bool overlaps_aabb(Aabb left_bounds, vec4 right_min_bounds, vec4 right_max_bounds)
{
    return all(lessThanEqual(left_bounds.min_bounds.xyz, right_max_bounds.xyz)) &&
           all(greaterThanEqual(left_bounds.max_bounds.xyz, right_min_bounds.xyz));
}

#endif
