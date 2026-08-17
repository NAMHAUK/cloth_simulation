#pragma once

#include "asset/AssetDataTypes.h"
#include "scene/Kinematics.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>
#include <glm/vec3.hpp>

struct SimulationGpuView;

class ExternalForceSolver final
{
public:
    ExternalForceSolver(float velocity_damping,
                        float reference_frame_inertia_scale,
                        float reference_frame_max_acceleration,
                        float reference_frame_max_angular_acceleration);
    ExternalForceSolver(const ExternalForceSolver&) = delete;
    ExternalForceSolver& operator=(const ExternalForceSolver&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, float dt, QOpenGLFunctions_4_5_Core& gl);
    void solve(const SimulationGpuView& views,
               GarmentLayer layer,
               const glm::vec3& external_acceleration,
               const Kinematics& reference_frame_kinematics,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_offset_location_ = -1;
    GLint vertex_count_location_ = -1;
    GLint delta_time_location_ = -1;
    GLint inverse_delta_time_location_ = -1;
    GLint external_acceleration_location_ = -1;
    GLint velocity_damping_location_ = -1;
    GLint frame_start_position_location_ = -1;
    GLint frame_end_position_location_ = -1;
    GLint frame_rotation_delta_location_ = -1;
    GLint frame_start_velocity_location_ = -1;
    GLint frame_acceleration_location_ = -1;
    GLint frame_start_angular_velocity_location_ = -1;
    GLint frame_angular_acceleration_location_ = -1;
    GLint frame_inertia_scale_location_ = -1;
    float dt_ = 0.0f;
    float inverse_dt_ = 0.0f;
    float velocity_damping_ = 0.0f;
    float reference_frame_inertia_scale_ = 0.0f;
    float reference_frame_max_acceleration_ = 0.0f;
    float reference_frame_max_angular_acceleration_ = 0.0f;
};
