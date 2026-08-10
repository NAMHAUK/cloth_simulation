#pragma once

#include <glm/vec3.hpp>

class SimulationForceField final
{
public:
    explicit SimulationForceField(float gravity);

    glm::vec3 external_acceleration() const;

private:
    float gravity_ = 0.0f;
};
