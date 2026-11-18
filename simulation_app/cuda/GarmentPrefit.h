#pragma once

#include <cstdint>

#include <vector_types.h>

struct PrefitParams;
struct TriangleBvhBuffers;

void prefit_garments(float4* positions,
                     std::uint32_t vertex_count,
                     const float4* body_positions,
                     const TriangleBvhBuffers& bvh,
                     const PrefitParams& params);
