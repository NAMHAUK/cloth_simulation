#ifndef COLLISION_CANDIDATE_OUTPUT_GLSL
#define COLLISION_CANDIDATE_OUTPUT_GLSL

shared uvec2 group_candidates[group_candidate_capacity];
shared uint group_candidate_count;
shared uint group_base_index;

void append_candidate(uvec2 candidate)
{
    uint group_candidate_index = atomicAdd(group_candidate_count, 1u);
    if (group_candidate_index < group_candidate_capacity) {
        group_candidates[group_candidate_index] = candidate;
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

    uint stored_candidate_count = min(group_candidate_count, group_candidate_capacity);
    if (gl_LocalInvocationIndex == 0u && stored_candidate_count > 0u) {
        group_base_index = atomicAdd(candidate_count, stored_candidate_count);
    }

    barrier();

    for (uint group_candidate_index = gl_LocalInvocationIndex;
         group_candidate_index < stored_candidate_count;
         group_candidate_index += gl_WorkGroupSize.x) {
        uint candidate_index = group_base_index + group_candidate_index;
        if (candidate_index < uMaxCandidateCount) {
            candidates[candidate_index] = group_candidates[group_candidate_index];
        }
    }
}

#endif
