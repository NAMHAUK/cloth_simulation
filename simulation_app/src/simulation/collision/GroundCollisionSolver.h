#pragma once

#include "gpu/cloth/ClothGpuResources.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct GroundCollisionParams;

class GroundCollisionSolver final
{
public:
    explicit GroundCollisionSolver(const GroundCollisionParams& params);
    GroundCollisionSolver(const GroundCollisionSolver&) = delete;
    GroundCollisionSolver& operator=(const GroundCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionPushoutBufferView& collision_pushout_view,
                   const ClothContactMotionBufferView& contact_motion_view) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionPushoutBufferView& collision_pushout_view,
               const ClothContactMotionBufferView& contact_motion_view,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_count_location_ = -1;
    GLint floor_height_location_ = -1;
    GLint static_friction_location_ = -1;
    GLint dynamic_friction_location_ = -1;
    float floor_height_ = 0.0f;
    float static_friction_ = 0.0f;
    float dynamic_friction_ = 0.0f;
};
