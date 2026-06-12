#pragma once

#include <cstdint>

namespace simulation_settings {
inline constexpr std::uint32_t character_motion_fps = 30;
inline constexpr std::uint32_t cloth_simulation_fps = 60;
inline constexpr std::uint32_t character_frame_stride = cloth_simulation_fps / character_motion_fps;
inline constexpr int simulation_tick_ms = 1000 / static_cast<int>(cloth_simulation_fps);

inline constexpr float fixed_dt = 1.0f / static_cast<float>(cloth_simulation_fps);
inline constexpr float gravity = -9.8f;

inline constexpr float ground_y = 0.0f;
inline constexpr float ground_collision_offset = 0.001f;
inline constexpr float character_collision_thickness = 0.02f;

inline constexpr std::uint32_t solver_iteration_count = 10;
inline constexpr float stretch_stiffness = 0.8f;
inline constexpr float bending_stiffness = 0.8f;

static_assert(character_motion_fps > 0);
static_assert(cloth_simulation_fps > 0);
static_assert(cloth_simulation_fps % character_motion_fps == 0);
static_assert(character_collision_thickness > 0.0f);
static_assert(solver_iteration_count > 0);
static_assert(stretch_stiffness >= 0.0f && stretch_stiffness <= 1.0f);
static_assert(bending_stiffness >= 0.0f && bending_stiffness <= 1.0f);
}
