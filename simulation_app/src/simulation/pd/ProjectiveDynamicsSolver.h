#pragma once

class PdGpuState;

class ProjectiveDynamicsSolver final
{
public:
    ProjectiveDynamicsSolver(const PdGpuState& pd_state, float gravity);
    ProjectiveDynamicsSolver(const ProjectiveDynamicsSolver&) = delete;
    ProjectiveDynamicsSolver& operator=(const ProjectiveDynamicsSolver&) = delete;

    void initialize(float substep_dt);
    void solve();
    void release();

private:
    const PdGpuState& pd_state_;
    const float gravity_;
    float substep_dt_ = 0.0f;
};
