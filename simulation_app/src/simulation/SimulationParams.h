#pragma once

#include "simulation/pbd/PbdSimulationParams.h"
#include "simulation/pd/PdSimulationParams.h"

#include <cstdint>

namespace simulation_limits {
inline constexpr std::uint32_t max_vertex_count = 500'000;
inline constexpr std::uint32_t max_constraint_count = 5'000'000;
}

enum class SimulationMode
{
    PBD,
    PD
};

struct SimulationStepParams final
{
    std::uint32_t motion_fps = 60;
    std::uint32_t fps = 60;

    constexpr std::uint32_t motion_stride() const { return fps / motion_fps; }

    constexpr float motion_frame_position(std::uint32_t motion_step_index, float substep_fraction) const
    {
        return (static_cast<float>(motion_step_index) + substep_fraction) / motion_stride();
    }

    constexpr float dt() const { return 1.0f / static_cast<float>(fps); }
};

struct PrefitParams final
{
    float surface_search_radius = 0.15f;
    float pushout_margin = 0.01f;
    float max_correction_length = 0.003f;
    std::uint32_t iteration_count = 32;
};

struct SimulationParams final
{
    SimulationMode mode = SimulationMode::PBD;
    SimulationStepParams step;
    PrefitParams prefit;
    PbdSimulationParams pbd;
    PdSimulationParams pd;

    float gravity = -9.8f;
    float max_ref_frame_linear_acceleration = 30.0f;
    float max_ref_frame_angular_acceleration = 60.0f;

    float ground_height = 0.001f;
    float attachment_surface_offset = 0.005f;
};

inline constexpr SimulationParams default_simulation_params{};

constexpr bool is_valid_simulation_params(const SimulationParams& params)
{
    const SimulationStepParams& step = params.step;
    const PrefitParams& prefit = params.prefit;

    return (params.mode == SimulationMode::PBD || params.mode == SimulationMode::PD) &&
           step.motion_fps > 0 &&
           step.fps > 0 &&
           step.fps % step.motion_fps == 0 &&
           params.max_ref_frame_linear_acceleration > 0.0f &&
           params.max_ref_frame_angular_acceleration > 0.0f &&
           params.attachment_surface_offset > 0.0f &&
           prefit.pushout_margin > 0.0f &&
           prefit.surface_search_radius >= prefit.pushout_margin &&
           prefit.max_correction_length > 0.0f &&
           prefit.iteration_count > 0 &&
           (params.mode == SimulationMode::PBD ? is_valid_simulation_params(params.pbd)
                                               : is_valid_simulation_params(params.pd));
}

static_assert(is_valid_simulation_params(default_simulation_params));
