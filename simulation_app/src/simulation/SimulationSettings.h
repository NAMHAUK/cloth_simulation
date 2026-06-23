#pragma once

#include <cstdint>

namespace simulation_settings {
inline constexpr std::uint32_t character_motion_fps = 60;
inline constexpr std::uint32_t cloth_simulation_fps = 60;
inline constexpr std::uint32_t character_frame_stride = cloth_simulation_fps / character_motion_fps;
inline constexpr int simulation_tick_ms = 1000 / static_cast<int>(cloth_simulation_fps);

inline constexpr float fixed_dt = 1.0f / static_cast<float>(cloth_simulation_fps);
inline constexpr float gravity = -9.8f;

inline constexpr float ground_y = 0.0f;
inline constexpr float ground_collision_offset = 0.001f;
inline constexpr float character_collision_thickness = 0.003f;
inline constexpr float character_collision_search_radius = 0.08f;

inline constexpr float prefit_pushout_margin = 0.02f;
inline constexpr float prefit_search_radius = 0.15f;

inline constexpr std::uint32_t substep_count = 8;
inline constexpr std::uint32_t solver_iteration_count = 4;
inline constexpr std::uint32_t prefit_iteration_count = 32;
inline constexpr float stretch_stiffness = 0.8f;
inline constexpr float bending_stiffness = 0.25f;
inline constexpr float attachment_stiffness = 0.8f;

static_assert(character_motion_fps > 0);
static_assert(cloth_simulation_fps > 0);
static_assert(cloth_simulation_fps % character_motion_fps == 0);
static_assert(character_collision_thickness > 0.0f);
static_assert(character_collision_search_radius >= character_collision_thickness);
static_assert(prefit_pushout_margin > 0.0f);
static_assert(prefit_search_radius >= prefit_pushout_margin);
static_assert(solver_iteration_count > 0);
static_assert(substep_count > 0);
static_assert(prefit_iteration_count > 0);
static_assert(stretch_stiffness >= 0.0f && stretch_stiffness <= 1.0f);
static_assert(bending_stiffness >= 0.0f && bending_stiffness <= 1.0f);
static_assert(attachment_stiffness >= 0.0f && attachment_stiffness <= 1.0f);
}
