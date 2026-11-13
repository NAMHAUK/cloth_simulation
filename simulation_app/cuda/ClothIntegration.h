#pragma once

#include <cstdint>

#include <vector_types.h>

void integrate_cloth(float4* positions,
                     float4* previous_positions,
                     float4* predicted_positions,
                     const float4* velocities,
                     std::uint32_t vertex_count,
                     float gravity,
                     float dt);

void update_cloth_velocities(const float4* positions,
                             const float4* previous_positions,
                             float4* velocities,
                             std::uint32_t vertex_count,
                             float dt);
