#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ClothMotionBufferView;

class AttachmentConstraintSolver final
{
public:
    AttachmentConstraintSolver() = default;
    AttachmentConstraintSolver(const AttachmentConstraintSolver&) = delete;
    AttachmentConstraintSolver& operator=(const AttachmentConstraintSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, float stiffness, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const AttachmentConstraintBufferView& constraint_view,
                   const TriangleGeometryResources& body_triangle_geometry) const;
    void solve(const ClothMotionBufferView& motion_view,
               const AttachmentConstraintBufferView& constraint_view,
               const TriangleGeometryResources& body_triangle_geometry,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint stiffness_location_ = -1;
    float stiffness_ = 0.0f;
};
