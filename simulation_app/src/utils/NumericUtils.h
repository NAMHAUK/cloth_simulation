#pragma once

#include <cmath>
#include <vector>

#include <glm/vec3.hpp>

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
