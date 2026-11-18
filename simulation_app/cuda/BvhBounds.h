#pragma once

#include <vector_types.h>

struct TriangleBvhBuffers;

void update_triangle_bvh_bounds(const TriangleBvhBuffers& bvh, const float4* positions, float detection_distance);
