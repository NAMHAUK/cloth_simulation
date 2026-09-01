#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ReferenceFrameKinematics;
class ClothGpuState;

class ClothIntegrator final
{
public:
    ClothIntegrator(float gravity,
                    float velocity_damping,
                    float reference_frame_inertia_scale,
                    float reference_frame_max_acceleration,
                    float reference_frame_max_angular_acceleration);
    ClothIntegrator(const ClothIntegrator&) = delete;
    ClothIntegrator& operator=(const ClothIntegrator&) = delete;

    void initialize(const std::filesystem::path& shader_dir, float dt, QOpenGLFunctions_4_5_Core& gl);
    void integrate(const ClothGpuState& cloth_state,
                   GarmentLayer layer,
                   const ReferenceFrameKinematics& reference_frame_kinematics,
                   QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_offset_loc_ = -1;
    GLint vertex_count_loc_ = -1;
    GLint frame_start_position_loc_ = -1;
    GLint frame_end_position_loc_ = -1;
    GLint frame_rotation_delta_loc_ = -1;
    GLint frame_start_velocity_loc_ = -1;
    GLint frame_acceleration_loc_ = -1;
    GLint frame_start_angular_velocity_loc_ = -1;
    GLint frame_angular_acceleration_loc_ = -1;
    float gravity_ = 0.0f;
    float velocity_damping_ = 0.0f;
    float reference_frame_inertia_scale_ = 0.0f;
    float reference_frame_max_acceleration_ = 0.0f;
    float reference_frame_max_angular_acceleration_ = 0.0f;
};
