#pragma once

#include "gpu/cloth/ClothGpuResources.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class GroundCollisionSolver final {
public:
    GroundCollisionSolver() = default;
    GroundCollisionSolver(const GroundCollisionSolver&) = delete;
    GroundCollisionSolver& operator=(const GroundCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, float floor_height, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothPositionBufferView& position_view) const;
    void solve(const ClothPositionBufferView& position_view, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_count_location_ = -1;
    GLint floor_height_location_ = -1;
    float floor_height_ = 0.0f;
};
