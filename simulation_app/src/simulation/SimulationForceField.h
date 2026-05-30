#pragma once

#include <glm/vec3.hpp>

class SimulationForceField final {
public:
    glm::vec3 external_acceleration() const;
};
