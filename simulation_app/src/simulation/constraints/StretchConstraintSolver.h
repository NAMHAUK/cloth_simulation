#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ClothPositionBufferView;
struct DistanceConstraintBufferView;

class StretchConstraintSolver final {
public:
    StretchConstraintSolver() = default;
    StretchConstraintSolver(const StretchConstraintSolver&) = delete;
    StretchConstraintSolver& operator=(const StretchConstraintSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, float stiffness, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothPositionBufferView& position_view, const DistanceConstraintBufferView& constraint_view) const;
    void solve(const ClothPositionBufferView& position_view,
               const DistanceConstraintBufferView& constraint_view,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint stiffness_location_ = -1;
    float stiffness_ = 0.0f;
};
