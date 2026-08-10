#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct SimulationGpuView;

class StretchConstraintSolver final
{
public:
    explicit StretchConstraintSolver(float stiffness);
    StretchConstraintSolver(const StretchConstraintSolver&) = delete;
    StretchConstraintSolver& operator=(const StretchConstraintSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuView& views) const;
    void solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint stiffness_location_ = -1;
    float stiffness_ = 0.0f;
};
