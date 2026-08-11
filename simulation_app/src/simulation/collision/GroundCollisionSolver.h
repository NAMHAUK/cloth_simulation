#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct GroundCollisionParams;
struct SimulationGpuView;

class GroundCollisionSolver final
{
public:
    explicit GroundCollisionSolver(const GroundCollisionParams& params);
    GroundCollisionSolver(const GroundCollisionSolver&) = delete;
    GroundCollisionSolver& operator=(const GroundCollisionSolver&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuView& views) const;
    void solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const;
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
