#pragma once

#include <cstdint>

class PdGpuState;

class ProjectiveDynamicsSolver final
{
public:
    ProjectiveDynamicsSolver(PdGpuState& pd_state,
                             float gravity,
                             float dt,
                             float velocity_damping,
                             std::uint32_t iteration_count,
                             float attachment_surface_offset,
                             float ground_height);
    ProjectiveDynamicsSolver(const ProjectiveDynamicsSolver&) = delete;
    ProjectiveDynamicsSolver& operator=(const ProjectiveDynamicsSolver&) = delete;

    void solve();

private:
    PdGpuState& pd_state_;
    const float gravity_;
    const float dt_;
    const float velocity_damping_;
    const float inv_dt_squared_;
    const std::uint32_t iteration_count_;
    const float attachment_surface_offset_;
    const float ground_height_;
};
