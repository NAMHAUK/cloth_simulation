#pragma once

#include "utils/NumericUtils.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

struct CharacterReferenceFrame final
{
    glm::vec3 position{};
    glm::quat orientation = quat_xyzw(0.0f, 0.0f, 0.0f, 1.0f);
};

struct Kinematics final
{
    // State
    void reset(const CharacterReferenceFrame& frame);
    void update(const CharacterReferenceFrame& frame, float dt);

    glm::vec3 start_position{};
    glm::vec3 end_position{};
    glm::mat3 rotation_delta{1.0f};
    glm::vec3 start_velocity{};
    glm::vec3 acceleration{};
    glm::vec3 start_angular_velocity{};
    glm::vec3 angular_acceleration{};

private:
    // History
    glm::quat orientation_ = quat_xyzw(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 velocity_{};
    glm::vec3 angular_velocity_{};
};
