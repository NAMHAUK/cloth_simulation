#include "simulation/forces/SimulationForceField.h"

#include "simulation/SimulationSettings.h"

glm::vec3 SimulationForceField::external_acceleration() const
{
    return {0.0f, simulation_settings::gravity, 0.0f};
}
