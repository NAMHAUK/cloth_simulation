#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;

class AttachmentConstraintSolver final
{
public:
    explicit AttachmentConstraintSolver(float stiffness);
    AttachmentConstraintSolver(const AttachmentConstraintSolver&) = delete;
    AttachmentConstraintSolver& operator=(const AttachmentConstraintSolver&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_loc_ = -1;
    GLint constraint_count_loc_ = -1;
    float stiffness_ = 0.0f;
};
