#pragma once

#include <cstdint>

#include <vector_types.h>

struct CudssSystem;

void solve_global(const CudssSystem& matrix, const float3* rhs, float4* positions, std::uint32_t vertex_count);
