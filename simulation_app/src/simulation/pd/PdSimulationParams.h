#pragma once

#include <cstdint>

struct PdSimulationParams final
{
    std::uint32_t substep_count = 8;
    std::uint32_t iteration_count = 2;
    float velocity_damping = 0.9835f;

    float body_detection_distance = 0.006f;
    static constexpr float body_collision_thickness = 0.005f;
};

constexpr bool is_valid_simulation_params(const PdSimulationParams& params)
{
    return params.substep_count > 0 &&
           params.iteration_count > 0 &&
           params.velocity_damping >= 0.0f &&
           params.velocity_damping <= 1.0f &&
           PdSimulationParams::body_collision_thickness > 0.0f &&
           params.body_detection_distance >= PdSimulationParams::body_collision_thickness;
}
