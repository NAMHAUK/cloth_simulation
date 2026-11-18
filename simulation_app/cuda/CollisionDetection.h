#pragma once

#include <vector_types.h>

class PdGpuState;

void detect_body_collision_candidates(const float4* positions, PdGpuState& state);
