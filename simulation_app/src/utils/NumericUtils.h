#pragma once

#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

inline glm::quat quat_xyzw(float x, float y, float z, float w)
{
    glm::quat value{};
    value.x = x;
    value.y = y;
    value.z = z;
    value.w = w;
    return value;
}

inline bool is_finite_vec3(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool is_finite_values(const std::vector<float>& values)
{
    for (float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}
