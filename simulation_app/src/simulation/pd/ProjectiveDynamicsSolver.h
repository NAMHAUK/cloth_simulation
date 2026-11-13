#pragma once

class PdGpuState;

class ProjectiveDynamicsSolver final
{
public:
    ProjectiveDynamicsSolver(PdGpuState& pd_state, float gravity, float dt);
    ProjectiveDynamicsSolver(const ProjectiveDynamicsSolver&) = delete;
    ProjectiveDynamicsSolver& operator=(const ProjectiveDynamicsSolver&) = delete;

    void solve();

private:
    PdGpuState& pd_state_;
    const float gravity_;
    const float dt_;
};
