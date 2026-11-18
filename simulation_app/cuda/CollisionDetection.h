#pragma once

#include <vector_types.h>

class PdGpuState;

void detect_body_collisions(const float4* positions, const float4* body_positions, PdGpuState& state);
