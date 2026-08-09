#include "simulation/forces/ExternalForceSolver.h"

#include "gpu/cloth/ClothGpuResources.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint velocities_binding = 2;
constexpr GLuint collision_pushouts_binding = 3;
constexpr GLuint cloth_cloth_pushouts_binding = 4;
constexpr GLuint contact_motion_deltas_binding = 5;
constexpr std::uint32_t external_force_local_size = 128;

glm::vec3 clamp_vector_length(const glm::vec3& value, float maximum_length)
{
    const float length = glm::length(value);
    return length > maximum_length ? value * (maximum_length / length) : value;
}
}

ExternalForceSolver::ExternalForceSolver(float velocity_damping,
                                         float frame_inertia_scale,
                                         float reference_frame_max_acceleration,
                                         float reference_frame_max_angular_acceleration)
    : velocity_damping_(velocity_damping),
      frame_inertia_scale_(frame_inertia_scale),
      reference_frame_max_acceleration_(reference_frame_max_acceleration),
      reference_frame_max_angular_acceleration_(reference_frame_max_angular_acceleration)
{}

bool ExternalForceSolver::is_initialized() const
{
    return program_ != 0;
}

bool ExternalForceSolver::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Cloth external force", gl);
    if (program_ == 0) {
        return false;
    }

    // shader program 안의 uniform 변수들 위치 저장
    vertex_offset_location_ = gl.glGetUniformLocation(program_, "uVertexOffset");
    vertex_count_location_ = gl.glGetUniformLocation(program_, "uVertexCount");
    delta_time_location_ = gl.glGetUniformLocation(program_, "uDeltaTime");
    inverse_delta_time_location_ = gl.glGetUniformLocation(program_, "uInverseDeltaTime");
    external_acceleration_location_ = gl.glGetUniformLocation(program_, "uExternalAcceleration");
    velocity_damping_location_ = gl.glGetUniformLocation(program_, "uVelocityDamping");
    frame_start_position_location_ = gl.glGetUniformLocation(program_, "uFrameStartPosition");
    frame_end_position_location_ = gl.glGetUniformLocation(program_, "uFrameEndPosition");
    frame_rotation_delta_location_ = gl.glGetUniformLocation(program_, "uFrameRotationDelta");
    frame_start_velocity_location_ = gl.glGetUniformLocation(program_, "uFrameStartVelocity");
    frame_acceleration_location_ = gl.glGetUniformLocation(program_, "uFrameAcceleration");
    frame_start_angular_velocity_location_ = gl.glGetUniformLocation(program_, "uFrameStartAngularVelocity");
    angular_acceleration_location_ = gl.glGetUniformLocation(program_, "uAngularAcceleration");
    frame_inertia_scale_location_ = gl.glGetUniformLocation(program_, "uFrameInertiaScale");

    if (vertex_offset_location_ < 0 ||
        vertex_count_location_ < 0 ||
        delta_time_location_ < 0 ||
        inverse_delta_time_location_ < 0 ||
        external_acceleration_location_ < 0 ||
        velocity_damping_location_ < 0 ||
        frame_start_position_location_ < 0 ||
        frame_end_position_location_ < 0 ||
        frame_rotation_delta_location_ < 0 ||
        frame_start_velocity_location_ < 0 ||
        frame_acceleration_location_ < 0 ||
        frame_start_angular_velocity_location_ < 0 ||
        angular_acceleration_location_ < 0 ||
        frame_inertia_scale_location_ < 0) {
        std::cerr << "Cloth external force compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

// 외부 힘 계산 -> 힘에 따른 위치 변화 GPU에서 갱신
void ExternalForceSolver::solve(const ClothMotionBufferView& motion_view,
                                const ClothCollisionPushoutBufferView& collision_pushout_view,
                                const ClothContactMotionBufferView& contact_motion_view,
                                const ElementRange& vertex_range,
                                float dt,
                                float inverse_dt,
                                const glm::vec3& external_acceleration,
                                const Kinematics& kinematics,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    const bool has_valid_vertices = vertex_range.count > 0u &&
                                    vertex_range.offset <= motion_view.vertex_count &&
                                    vertex_range.count <= motion_view.vertex_count - vertex_range.offset;
    if (!is_initialized() ||
        !is_valid_motion_view(motion_view) ||
        !is_valid_collision_pushout_view(collision_pushout_view) ||
        !is_valid_contact_motion_view(contact_motion_view) ||
        motion_view.vertex_count != collision_pushout_view.vertex_count ||
        motion_view.vertex_count != contact_motion_view.vertex_count ||
        !has_valid_vertices ||
        dt <= 0.0f ||
        inverse_dt <= 0.0f) {
        return;
    }

    const glm::vec3 frame_acceleration =
        clamp_vector_length(kinematics.acceleration, reference_frame_max_acceleration_);
    const glm::vec3 frame_angular_acceleration =
        clamp_vector_length(kinematics.angular_acceleration, reference_frame_max_angular_acceleration_);

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, velocities_binding, motion_view.velocity_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        collision_pushouts_binding,
                        collision_pushout_view.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_pushouts_binding,
                        collision_pushout_view.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        contact_motion_deltas_binding,
                        contact_motion_view.contact_motion_delta_buffer);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_offset_location_, vertex_range.offset);
    gl.glProgramUniform1ui(program_, vertex_count_location_, vertex_range.count);
    gl.glProgramUniform1f(program_, delta_time_location_, dt);
    gl.glProgramUniform1f(program_, inverse_delta_time_location_, inverse_dt);
    gl.glProgramUniform3f(program_,
                          external_acceleration_location_,
                          external_acceleration.x,
                          external_acceleration.y,
                          external_acceleration.z);
    gl.glProgramUniform1f(program_, velocity_damping_location_, velocity_damping_);
    gl.glProgramUniform3f(program_,
                          frame_start_position_location_,
                          kinematics.start_position.x,
                          kinematics.start_position.y,
                          kinematics.start_position.z);
    gl.glProgramUniform3f(program_,
                          frame_end_position_location_,
                          kinematics.end_position.x,
                          kinematics.end_position.y,
                          kinematics.end_position.z);
    gl.glProgramUniformMatrix3fv(program_,
                                 frame_rotation_delta_location_,
                                 1,
                                 GL_FALSE,
                                 glm::value_ptr(kinematics.rotation_delta));
    gl.glProgramUniform3f(program_,
                          frame_start_velocity_location_,
                          kinematics.start_velocity.x,
                          kinematics.start_velocity.y,
                          kinematics.start_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_acceleration_location_,
                          frame_acceleration.x,
                          frame_acceleration.y,
                          frame_acceleration.z);
    gl.glProgramUniform3f(program_,
                          frame_start_angular_velocity_location_,
                          kinematics.start_angular_velocity.x,
                          kinematics.start_angular_velocity.y,
                          kinematics.start_angular_velocity.z);
    gl.glProgramUniform3f(program_,
                          angular_acceleration_location_,
                          frame_angular_acceleration.x,
                          frame_angular_acceleration.y,
                          frame_angular_acceleration.z);
    gl.glProgramUniform1f(program_, frame_inertia_scale_location_, frame_inertia_scale_);

    // shader가 외부 가속도에 따른 위치 변화량 계산 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(vertex_range.count, external_force_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ExternalForceSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_location_ = -1;
    vertex_count_location_ = -1;
    delta_time_location_ = -1;
    inverse_delta_time_location_ = -1;
    external_acceleration_location_ = -1;
    velocity_damping_location_ = -1;
    frame_start_position_location_ = -1;
    frame_end_position_location_ = -1;
    frame_rotation_delta_location_ = -1;
    frame_start_velocity_location_ = -1;
    frame_acceleration_location_ = -1;
    frame_start_angular_velocity_location_ = -1;
    angular_acceleration_location_ = -1;
    frame_inertia_scale_location_ = -1;
}
