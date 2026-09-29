#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ClothIntegrationParams;
struct GarmentBufferState;
struct ReferenceFrameKinematics;
class ClothGpuState;
class SceneState;

class ClothIntegrator final
{
public:
    explicit ClothIntegrator(const ClothIntegrationParams& params);
    ClothIntegrator(const ClothIntegrator&) = delete;
    ClothIntegrator& operator=(const ClothIntegrator&) = delete;

    void initialize(const std::filesystem::path& shader_dir, float dt, QOpenGLFunctions_4_5_Core& gl);
    void integrate(const ClothGpuState& cloth_state,
                   const SceneState& scene,
                   QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void integrate_garment(const GarmentBufferState& garment_state,
                           const ReferenceFrameKinematics& kinematics,
                           QOpenGLFunctions_4_5_Core& gl) const;

    GLuint program_ = 0;
    GLint vertex_offset_loc_ = -1;
    GLint vertex_count_loc_ = -1;
    GLint frame_start_position_loc_ = -1;
    GLint frame_end_position_loc_ = -1;
    GLint frame_rotation_delta_loc_ = -1;
    GLint frame_start_linear_velocity_loc_ = -1;
    GLint frame_linear_acceleration_loc_ = -1;
    GLint frame_start_angular_velocity_loc_ = -1;
    GLint frame_angular_acceleration_loc_ = -1;
    float gravity_ = 0.0f;
    float velocity_damping_ = 0.0f;
    float frame_inertia_scale_ = 0.0f;
};
