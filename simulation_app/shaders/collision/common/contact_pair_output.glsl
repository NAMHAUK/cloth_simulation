#ifndef CONTACT_PAIR_OUTPUT_GLSL
#define CONTACT_PAIR_OUTPUT_GLSL

shared uvec2 group_pairs[group_pair_capacity];
shared uint group_pair_count;
shared uint group_base_index;

void append_group_overflow_pair(uvec2 pair_record)
{
    uint pair_index = atomicAdd(pair_count, 1u);
    if (pair_index >= uMaxPairCount) {
        atomicAdd(overflow_count, 1u);
        return;
    }

    pair_records[pair_index] = pair_record;
}

void append_pair(uvec2 pair_record)
{
    uint group_pair_index = atomicAdd(group_pair_count, 1u);
    if (group_pair_index < group_pair_capacity) {
        group_pairs[group_pair_index] = pair_record;
    } else {
        append_group_overflow_pair(pair_record);
    }
}

void flush_group_pairs()
{
    barrier();

    uint stored_pair_count = min(group_pair_count, group_pair_capacity);
    if (gl_LocalInvocationIndex == 0u) {
        if (stored_pair_count == 0u) {
            group_base_index = 0u;
        } else {
            group_base_index = atomicAdd(pair_count, stored_pair_count);
            if (group_base_index >= uMaxPairCount) {
                atomicAdd(overflow_count, stored_pair_count);
            } else {
                uint available_pair_count = uMaxPairCount - group_base_index;
                if (stored_pair_count > available_pair_count) {
                    atomicAdd(overflow_count, stored_pair_count - available_pair_count);
                }
            }
        }
    }

    barrier();

    for (uint group_pair_index = gl_LocalInvocationIndex;
         group_pair_index < stored_pair_count;
         group_pair_index += gl_WorkGroupSize.x) {
        uint pair_index = group_base_index + group_pair_index;
        if (pair_index < uMaxPairCount) {
            pair_records[pair_index] = group_pairs[group_pair_index];
        }
    }
}

#endif
