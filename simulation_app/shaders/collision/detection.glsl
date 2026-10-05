#ifndef COLLISION_DETECTION_GLSL
#define COLLISION_DETECTION_GLSL

const uint max_bvh_stack_depth = 32u;
const uint group_candidate_capacity = 512u;

shared uvec2 group_candidates[group_candidate_capacity];
shared uint group_candidate_count;
shared uint group_base_index;

void append_candidate(uvec2 candidate)
{
    uint local_index = atomicAdd(group_candidate_count, 1u);
    if (local_index < group_candidate_capacity) {
        group_candidates[local_index] = candidate;
        return;
    }

    uint candidate_index = atomicAdd(candidate_count, 1u);
    if (candidate_index < uMaxCandidateCount) {
        candidates[candidate_index] = candidate;
    }
}

void flush_group_candidates()
{
    barrier();

    uint local_count = min(group_candidate_count, group_candidate_capacity);
    if (gl_LocalInvocationIndex == 0u && local_count > 0u) {
        group_base_index = atomicAdd(candidate_count, local_count);
    }

    barrier();

    for (uint local_index = gl_LocalInvocationIndex; local_index < local_count; local_index += gl_WorkGroupSize.x) {
        uint candidate_index = group_base_index + local_index;
        if (candidate_index < uMaxCandidateCount) {
            candidates[candidate_index] = group_candidates[local_index];
        }
    }
}

void detect_candidates(DetectionInput detection_input, uint bvh_root)
{
    uint node_index_stack[max_bvh_stack_depth];
    uint stack_count = 0u;
    uint node_index = bvh_root;
    bool has_next_node = overlaps_aabb(detection_input.bounds, bvh_nodes[node_index].bounds);

    while (has_next_node) {
        BvhNode node = bvh_nodes[node_index];
        if (is_leaf_node(node)) {
            detect_leaf_candidates(detection_input, node);
        } else {
            bvec2 can_visit_children = select_children_to_visit(detection_input, node);
            if (can_visit_children.x && can_visit_children.y) {
                node_index_stack[stack_count++] = node.left_child_index;
            }
            if (can_visit_children.x || can_visit_children.y) {
                node_index = can_visit_children.y ? node.right_child_index : node.left_child_index;
                continue;
            }
        }

        has_next_node = stack_count > 0u;
        if (has_next_node) {
            node_index = node_index_stack[--stack_count];
        }
    }
}

#endif
