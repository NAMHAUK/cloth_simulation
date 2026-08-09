#include "simulation/forces/SimulationForceField.h"

SimulationForceField::SimulationForceField(float gravity) : gravity_(gravity)
{}

glm::vec3 SimulationForceField::external_acceleration() const
{
    return {0.0f, gravity_, 0.0f};
}
