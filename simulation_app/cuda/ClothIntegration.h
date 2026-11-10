#pragma once

#include <cstdint>

#include <cuda_runtime_api.h>
#include <vector_types.h>

cudaError_t integrate_cloth(float4* positions, float4* velocities, std::uint32_t vertex_count, float gravity, float dt);
