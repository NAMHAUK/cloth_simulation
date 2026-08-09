#include "scene/Kinematics.h"

#include "scene/SceneState.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
constexpr float angular_velocity_epsilon = 1.0e-8f;

glm::vec3 angular_velocity(const glm::quat& start_orientation, const glm::quat& end_orientation, float dt)
{
    glm::quat delta = glm::normalize(end_orientation * glm::conjugate(start_orientation));
    if (delta.w < 0.0f) {
        delta = -delta;
    }

    const glm::vec3 vector{delta.x, delta.y, delta.z};
    const float vector_length = glm::length(vector);
    if (vector_length <= angular_velocity_epsilon || dt <= 0.0f) {
        return glm::vec3{0.0f};
    }

    const float angle = 2.0f * std::atan2(vector_length, std::clamp(delta.w, -1.0f, 1.0f));
    return vector * (angle / (vector_length * dt));
}

}

void Kinematics::reset(const CharacterReferenceFrame& frame)
{
    start_position = frame.position;
    end_position = frame.position;
    rotation_delta = glm::mat3{1.0f};
    start_velocity = glm::vec3{0.0f};
    acceleration = glm::vec3{0.0f};
    start_angular_velocity = glm::vec3{0.0f};
    angular_acceleration = glm::vec3{0.0f};
    orientation_ = frame.orientation;
    velocity_ = glm::vec3{0.0f};
    angular_velocity_ = glm::vec3{0.0f};
}

void Kinematics::update(const CharacterReferenceFrame& frame, float dt)
{
    assert(dt > 0.0f);

    const glm::vec3 end_velocity = (frame.position - end_position) / dt;
    const glm::vec3 end_angular_velocity = angular_velocity(orientation_, frame.orientation, dt);

    start_position = end_position;
    end_position = frame.position;
    rotation_delta = glm::mat3_cast(glm::normalize(frame.orientation * glm::conjugate(orientation_)));
    start_velocity = velocity_;
    acceleration = (end_velocity - velocity_) / dt;
    start_angular_velocity = angular_velocity_;
    angular_acceleration = (end_angular_velocity - angular_velocity_) / dt;

    orientation_ = frame.orientation;
    velocity_ = end_velocity;
    angular_velocity_ = end_angular_velocity;
}
