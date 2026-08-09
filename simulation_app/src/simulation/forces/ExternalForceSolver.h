#pragma once

#include "scene/Kinematics.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>
#include <glm/vec3.hpp>

struct ClothMotionBufferView;
struct ClothCollisionPushoutBufferView;
struct ClothContactMotionBufferView;
struct ElementRange;

class ExternalForceSolver final
{
public:
    ExternalForceSolver(float velocity_damping,
                        float frame_inertia_scale,
                        float reference_frame_max_acceleration,
                        float reference_frame_max_angular_acceleration);
    ExternalForceSolver(const ExternalForceSolver&) = delete;
    ExternalForceSolver& operator=(const ExternalForceSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionPushoutBufferView& collision_pushout_view,
               const ClothContactMotionBufferView& contact_motion_view,
               const ElementRange& vertex_range,
               float dt,
               float inverse_dt,
               const glm::vec3& external_acceleration,
               const Kinematics& kinematics,
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
    GLint angular_acceleration_location_ = -1;
    GLint frame_inertia_scale_location_ = -1;
    float velocity_damping_ = 0.0f;
    float frame_inertia_scale_ = 0.0f;
    float reference_frame_max_acceleration_ = 0.0f;
    float reference_frame_max_angular_acceleration_ = 0.0f;
};
