#pragma once

#include <cstdint>

struct SimulationStepParams final
{
    std::uint32_t motion_fps = 60;
    std::uint32_t fps = 60;
    std::uint32_t substep_count = 8;
    std::uint32_t iteration_count = 8;

    constexpr std::uint32_t motion_stride() const { return fps / motion_fps; }

    constexpr float motion_frame_position(std::uint32_t motion_step_index, std::uint32_t substep) const
    {
        const float substep_fraction = static_cast<float>(substep) / substep_count;
        return (static_cast<float>(motion_step_index) + substep_fraction) / motion_stride();
    }

    constexpr float dt() const { return 1.0f / static_cast<float>(fps); }
};

struct ClothIntegrationParams final
{
    float gravity = -9.8f;
    float velocity_damping = 0.99792f;
    float reference_frame_inertia_scale = 0.5f;
    float reference_frame_max_linear_acceleration = 30.0f;
    float reference_frame_max_angular_acceleration = 60.0f;
};

struct ConstraintParams final
{
    float stretch_stiffness = 0.8f;
    float bending_stiffness = 0.15f;
    float attachment_stiffness = 0.8f;
    float attachment_surface_offset = 0.005f;
};

struct GroundCollisionParams final
{
    float height = 0.001f;
    float static_friction = 0.55f;
    float dynamic_friction = 0.47f;
};

struct BodyCollisionParams final
{
    float detection_distance = 0.005f;
    float thickness = 0.005f;
    float max_correction_length = 0.005f;

    // Defaults target dry cotton fabric against a skin-like body surface:
    // reported kinetic COF is about 0.46-0.58, and dynamic COF is commonly
    // lower than static COF by about 0.85 in textile contact references.
    float static_friction = 0.55f;
    float dynamic_friction = 0.47f;
};

struct ClothCollisionParams final
{
    float initial_detection_distance = 0.012f;
    float thickness = 0.007f;
    float detection_distance = 0.009f;
    float stiffness = 1.0f;
    float max_correction_length = 0.003f;
    float body_search_radius = 0.15f;
};

struct CollisionParams final
{
    GroundCollisionParams ground;
    BodyCollisionParams body;
    ClothCollisionParams cloth;
};

struct PrefitParams final
{
    float surface_search_radius = 0.15f;
    float pushout_margin = 0.01f;
    std::uint32_t iteration_count = 32;
};

struct SimulationParams final
{
    SimulationStepParams step;
    ClothIntegrationParams integration;
    ConstraintParams constraints;
    CollisionParams collisions;
    PrefitParams prefit;
};

inline constexpr SimulationParams default_simulation_params{};

constexpr bool is_valid_simulation_params(const SimulationParams& params)
{
    const SimulationStepParams& step = params.step;
    const ClothIntegrationParams& integration = params.integration;
    const ConstraintParams& constraints = params.constraints;
    const GroundCollisionParams& ground = params.collisions.ground;
    const BodyCollisionParams& body = params.collisions.body;
    const ClothCollisionParams& cloth = params.collisions.cloth;
    const PrefitParams& prefit = params.prefit;

    return step.motion_fps > 0 &&
           step.fps > 0 &&
           step.fps % step.motion_fps == 0 &&
           step.substep_count > 0 &&
           step.iteration_count > 0 &&
           integration.velocity_damping >= 0.0f &&
           integration.velocity_damping <= 1.0f &&
           integration.reference_frame_inertia_scale >= 0.0f &&
           integration.reference_frame_inertia_scale <= 1.0f &&
           integration.reference_frame_max_linear_acceleration > 0.0f &&
           integration.reference_frame_max_angular_acceleration > 0.0f &&
           constraints.stretch_stiffness >= 0.0f &&
           constraints.stretch_stiffness <= 1.0f &&
           constraints.bending_stiffness >= 0.0f &&
           constraints.bending_stiffness <= 1.0f &&
           constraints.attachment_stiffness >= 0.0f &&
           constraints.attachment_stiffness <= 1.0f &&
           constraints.attachment_surface_offset > 0.0f &&
           ground.dynamic_friction >= 0.0f &&
           ground.static_friction >= ground.dynamic_friction &&
           ground.static_friction <= 1.0f &&
           body.thickness > 0.0f &&
           body.detection_distance >= body.thickness &&
           body.max_correction_length > 0.0f &&
           body.dynamic_friction >= 0.0f &&
           body.static_friction >= body.dynamic_friction &&
           body.static_friction <= 1.0f &&
           cloth.thickness > 0.0f &&
           cloth.detection_distance >= cloth.thickness &&
           cloth.initial_detection_distance >= cloth.thickness &&
           cloth.stiffness >= 0.0f &&
           cloth.stiffness <= 1.0f &&
           cloth.max_correction_length > 0.0f &&
           cloth.body_search_radius > 0.0f &&
           prefit.pushout_margin > 0.0f &&
           prefit.surface_search_radius >= prefit.pushout_margin &&
           prefit.iteration_count > 0;
}

static_assert(is_valid_simulation_params(default_simulation_params));
