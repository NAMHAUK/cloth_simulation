#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;

class StretchConstraintSolver final
{
public:
    explicit StretchConstraintSolver(float stiffness);
    StretchConstraintSolver(const StretchConstraintSolver&) = delete;
    StretchConstraintSolver& operator=(const StretchConstraintSolver&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_loc_ = -1;
    GLint constraint_count_loc_ = -1;
    float stiffness_ = 0.0f;
};
