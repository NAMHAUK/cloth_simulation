#pragma once

#include <cstdint>

class PdGpuState;
struct SimulationParams;

class ProjectiveDynamicsSolver final
{
public:
    ProjectiveDynamicsSolver(PdGpuState& pd_state, const SimulationParams& params, float dt);
    ProjectiveDynamicsSolver(const ProjectiveDynamicsSolver&) = delete;
    ProjectiveDynamicsSolver& operator=(const ProjectiveDynamicsSolver&) = delete;

    void solve();

private:
    PdGpuState& pd_state_;
    const float gravity_;
    const float dt_;
    const float inv_dt_squared_;
    const float velocity_damping_;
    const std::uint32_t iteration_count_;
    const float attachment_surface_offset_;
    const float ground_height_;
};
