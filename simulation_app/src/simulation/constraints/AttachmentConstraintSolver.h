#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct SimulationGpuView;

class AttachmentConstraintSolver final
{
public:
    explicit AttachmentConstraintSolver(float stiffness);
    AttachmentConstraintSolver(const AttachmentConstraintSolver&) = delete;
    AttachmentConstraintSolver& operator=(const AttachmentConstraintSolver&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
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
