#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct BendingConstraintBufferView;
struct ClothPositionBufferView;

class BendingConstraintSolver final {
public:
    BendingConstraintSolver() = default;
    BendingConstraintSolver(const BendingConstraintSolver&) = delete;
    BendingConstraintSolver& operator=(const BendingConstraintSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothPositionBufferView& position_view,
                   const BendingConstraintBufferView& constraint_view,
                   float stiffness) const;
    void solve(const ClothPositionBufferView& position_view,
               const BendingConstraintBufferView& constraint_view,
               float stiffness,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint stiffness_location_ = -1;
};
