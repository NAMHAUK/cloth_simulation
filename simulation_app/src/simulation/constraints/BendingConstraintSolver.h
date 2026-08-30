#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;

class BendingConstraintSolver final
{
public:
    explicit BendingConstraintSolver(float stiffness);
    BendingConstraintSolver(const BendingConstraintSolver&) = delete;
    BendingConstraintSolver& operator=(const BendingConstraintSolver&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothGpuState& cloth_state) const;
    void solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint stiffness_location_ = -1;
    float stiffness_ = 0.0f;
};
