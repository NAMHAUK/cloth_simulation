#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

struct CharacterReferenceFrame;

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
    glm::quat orientation_ = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 velocity_{};
    glm::vec3 angular_velocity_{};
};
