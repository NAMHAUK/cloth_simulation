#pragma once

#include <cstdint>

struct ClothIntegrationParams final
{
    float velocity_damping = 0.99792f;
    float reference_frame_inertia_scale = 0.5f;
};

struct ConstraintParams final
{
    float stretch_stiffness = 0.8f;
    float bending_stiffness = 0.15f;
    float attachment_stiffness = 0.8f;
};

struct GroundCollisionParams final
{
    float static_friction = 0.55f;
    float dynamic_friction = 0.47f;
};

struct BodyCollisionParams final
{
    float detection_distance = 0.006f;
    float thickness = 0.005f;
    float max_correction_length = 0.005f;
    float static_friction = 0.55f;
    float dynamic_friction = 0.47f;
};

struct ClothCollisionParams final
{
    float detection_distance = 0.004f;
    float thickness = 0.003f;
    float stiffness = 1.0f;
    float max_correction_length = 0.003f;
    float initial_detection_distance = 0.012f;
    float body_search_radius = 0.15f;
};

struct CollisionParams final
{
    GroundCollisionParams ground;
    BodyCollisionParams body;
    ClothCollisionParams cloth;
};

struct PbdSimulationParams final
{
    std::uint32_t substep_count = 8;
    std::uint32_t iteration_count = 8;
    ClothIntegrationParams integration;
    ConstraintParams constraints;
    CollisionParams collisions;
};

constexpr bool is_valid_simulation_params(const PbdSimulationParams& params)
{
    const ClothIntegrationParams& integration = params.integration;
    const ConstraintParams& constraints = params.constraints;
    const GroundCollisionParams& ground = params.collisions.ground;
    const BodyCollisionParams& body = params.collisions.body;
    const ClothCollisionParams& cloth = params.collisions.cloth;

    return params.substep_count > 0 &&
           params.iteration_count > 0 &&
           integration.velocity_damping >= 0.0f &&
           integration.velocity_damping <= 1.0f &&
           integration.reference_frame_inertia_scale >= 0.0f &&
           integration.reference_frame_inertia_scale <= 1.0f &&
           constraints.stretch_stiffness >= 0.0f &&
           constraints.stretch_stiffness <= 1.0f &&
           constraints.bending_stiffness >= 0.0f &&
           constraints.bending_stiffness <= 1.0f &&
           constraints.attachment_stiffness >= 0.0f &&
           constraints.attachment_stiffness <= 1.0f &&
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
           cloth.body_search_radius > 0.0f;
}
