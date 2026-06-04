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

inline constexpr std::uint32_t constraint_solver_iterations = 10;
inline constexpr float stretch_constraint_stiffness = 0.8f;
inline constexpr float bending_constraint_stiffness = 0.8f;

static_assert(character_motion_fps > 0);
static_assert(cloth_simulation_fps > 0);
static_assert(cloth_simulation_fps % character_motion_fps == 0);
static_assert(constraint_solver_iterations > 0);
static_assert(stretch_constraint_stiffness >= 0.0f && stretch_constraint_stiffness <= 1.0f);
static_assert(bending_constraint_stiffness >= 0.0f && bending_constraint_stiffness <= 1.0f);
}
