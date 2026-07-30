#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>
#include <glm/vec3.hpp>

struct ClothMotionBufferView;
struct ClothCollisionPushoutBufferView;

class ExternalForceSolver final {
public:
    ExternalForceSolver() = default;
    ExternalForceSolver(const ExternalForceSolver&) = delete;
    ExternalForceSolver& operator=(const ExternalForceSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionPushoutBufferView& collision_pushout_view,
               float dt,
               const glm::vec3& external_acceleration,
               float velocity_damping,
               const glm::vec3& root_translation,
               const glm::vec3& previous_root_velocity,
               const glm::vec3& root_acceleration,
               float root_inertia_scale,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_count_location_ = -1;
    GLint delta_time_location_ = -1;
    GLint external_acceleration_location_ = -1;
    GLint velocity_damping_location_ = -1;
    GLint root_translation_location_ = -1;
    GLint previous_root_velocity_location_ = -1;
    GLint root_acceleration_location_ = -1;
    GLint root_inertia_scale_location_ = -1;
};
