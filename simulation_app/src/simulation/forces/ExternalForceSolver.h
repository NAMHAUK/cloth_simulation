#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

struct ClothMotionBufferView;
struct ClothCollisionPushoutBufferView;
struct ClothContactMotionBufferView;
struct GarmentBufferRanges;

struct ReferenceFrameMotion final
{
    glm::vec3 start_position{};
    glm::vec3 end_position{};
    glm::mat3 rotation{1.0f};
    glm::vec3 previous_velocity{};
    glm::vec3 acceleration{};
    glm::vec3 previous_angular_velocity{};
    glm::vec3 angular_acceleration{};
};

class ExternalForceSolver final
{
public:
    ExternalForceSolver() = default;
    ExternalForceSolver(const ExternalForceSolver&) = delete;
    ExternalForceSolver& operator=(const ExternalForceSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionPushoutBufferView& collision_pushout_view,
               const ClothContactMotionBufferView& contact_motion_view,
               const GarmentBufferRanges& garment_range,
               float dt,
               float inverse_dt,
               const glm::vec3& external_acceleration,
               float velocity_damping,
               const ReferenceFrameMotion& frame_motion,
               float frame_inertia_scale,
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
    GLint frame_rotation_location_ = -1;
    GLint previous_frame_velocity_location_ = -1;
    GLint frame_acceleration_location_ = -1;
    GLint previous_angular_velocity_location_ = -1;
    GLint angular_acceleration_location_ = -1;
    GLint frame_inertia_scale_location_ = -1;
};
