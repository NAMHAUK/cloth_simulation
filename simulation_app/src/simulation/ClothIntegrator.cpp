#include "simulation/ClothIntegrator.h"

#include "gpu/cloth/ClothGpuState.h"
#include "gpu/scene/SimulationGpuView.h"
#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>

namespace {
constexpr std::uint32_t integration_local_size = 128;

glm::vec3 clamp_vector_length(const glm::vec3& value, float maximum_length)
{
    const float length = glm::length(value);
    return length > maximum_length ? value * (maximum_length / length) : value;
}
}

ClothIntegrator::ClothIntegrator(float gravity,
                                 float velocity_damping,
                                 float reference_frame_inertia_scale,
                                 float reference_frame_max_acceleration,
                                 float reference_frame_max_angular_acceleration)
    : gravity_(gravity),
      velocity_damping_(velocity_damping),
      reference_frame_inertia_scale_(reference_frame_inertia_scale),
      reference_frame_max_acceleration_(reference_frame_max_acceleration),
      reference_frame_max_angular_acceleration_(reference_frame_max_angular_acceleration)
{}

bool ClothIntegrator::is_initialized() const
{
    return program_ != 0;
}

void ClothIntegrator::initialize(const std::filesystem::path& shader_dir,
                                 float dt,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    if (dt <= 0.0f) {
        throw std::runtime_error("Cannot initialize cloth integrator with a non-positive time step.");
    }

    program_ = load_compute_program(shader_dir / "cloth" / "integrate_cloth.comp", gl);
    // shader program 안의 uniform 변수들 위치 저장
    vertex_offset_location_ = require_uniform_location(program_, "uVertexOffset", gl);
    vertex_count_location_ = require_uniform_location(program_, "uVertexCount", gl);
    delta_time_location_ = require_uniform_location(program_, "uDeltaTime", gl);
    inverse_delta_time_location_ = require_uniform_location(program_, "uInverseDeltaTime", gl);
    external_acceleration_location_ = require_uniform_location(program_, "uExternalAcceleration", gl);
    velocity_damping_location_ = require_uniform_location(program_, "uVelocityDamping", gl);
    frame_start_position_location_ = require_uniform_location(program_, "uFrameStartPosition", gl);
    frame_end_position_location_ = require_uniform_location(program_, "uFrameEndPosition", gl);
    frame_rotation_delta_location_ = require_uniform_location(program_, "uFrameRotationDelta", gl);
    frame_start_velocity_location_ = require_uniform_location(program_, "uFrameStartVelocity", gl);
    frame_acceleration_location_ = require_uniform_location(program_, "uFrameAcceleration", gl);
    frame_start_angular_velocity_location_ =
        require_uniform_location(program_, "uFrameStartAngularVelocity", gl);
    frame_angular_acceleration_location_ =
        require_uniform_location(program_, "uFrameAngularAcceleration", gl);
    frame_inertia_scale_location_ = require_uniform_location(program_, "uFrameInertiaScale", gl);

    dt_ = dt;
    inverse_dt_ = 1.0f / dt_;
}

// 외부 힘 계산 -> 힘에 따른 위치 변화 GPU에서 갱신
void ClothIntegrator::integrate(const SimulationGpuView& views,
                                GarmentLayer layer,
                                const ReferenceFrameKinematics& reference_frame_kinematics,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& motion_view = views.cloth_motion;
    const auto& collision_pushout_view = views.cloth_collision_pushout;
    const auto& contact_motion_view = views.cloth_contact_motion;
    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
    const bool has_valid_vertices = is_valid_buffer_access(garment_state.vertex_start_index,
                                                           garment_state.vertex_count,
                                                           motion_view.vertex_count);
    if (!is_initialized() ||
        !is_valid_motion_view(motion_view) ||
        !is_valid_collision_pushout_view(collision_pushout_view) ||
        !is_valid_contact_motion_view(contact_motion_view) ||
        motion_view.vertex_count != collision_pushout_view.vertex_count ||
        motion_view.vertex_count != contact_motion_view.vertex_count ||
        !has_valid_vertices ||
        dt_ <= 0.0f ||
        inverse_dt_ <= 0.0f) {
        return;
    }

    const glm::vec3 frame_acceleration =
        clamp_vector_length(reference_frame_kinematics.acceleration, reference_frame_max_acceleration_);
    const glm::vec3 frame_angular_acceleration =
        clamp_vector_length(reference_frame_kinematics.angular_acceleration,
                            reference_frame_max_angular_acceleration_);

    // shader & GPU 연결
    gl.glUseProgram(program_);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_offset_location_, garment_state.vertex_start_index);
    gl.glProgramUniform1ui(program_, vertex_count_location_, garment_state.vertex_count);
    gl.glProgramUniform1f(program_, delta_time_location_, dt_);
    gl.glProgramUniform1f(program_, inverse_delta_time_location_, inverse_dt_);
    gl.glProgramUniform3f(program_, external_acceleration_location_, 0.0f, gravity_, 0.0f);
    gl.glProgramUniform1f(program_, velocity_damping_location_, velocity_damping_);
    gl.glProgramUniform3f(program_,
                          frame_start_position_location_,
                          reference_frame_kinematics.start_position.x,
                          reference_frame_kinematics.start_position.y,
                          reference_frame_kinematics.start_position.z);
    gl.glProgramUniform3f(program_,
                          frame_end_position_location_,
                          reference_frame_kinematics.end_position.x,
                          reference_frame_kinematics.end_position.y,
                          reference_frame_kinematics.end_position.z);
    gl.glProgramUniformMatrix3fv(program_,
                                 frame_rotation_delta_location_,
                                 1,
                                 GL_FALSE,
                                 glm::value_ptr(reference_frame_kinematics.rotation_delta));
    gl.glProgramUniform3f(program_,
                          frame_start_velocity_location_,
                          reference_frame_kinematics.start_velocity.x,
                          reference_frame_kinematics.start_velocity.y,
                          reference_frame_kinematics.start_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_acceleration_location_,
                          frame_acceleration.x,
                          frame_acceleration.y,
                          frame_acceleration.z);
    gl.glProgramUniform3f(program_,
                          frame_start_angular_velocity_location_,
                          reference_frame_kinematics.start_angular_velocity.x,
                          reference_frame_kinematics.start_angular_velocity.y,
                          reference_frame_kinematics.start_angular_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_angular_acceleration_location_,
                          frame_angular_acceleration.x,
                          frame_angular_acceleration.y,
                          frame_angular_acceleration.z);
    gl.glProgramUniform1f(program_, frame_inertia_scale_location_, reference_frame_inertia_scale_);

    // shader가 외부 가속도에 따른 위치 변화량 계산 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, integration_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothIntegrator::release(QOpenGLFunctions_4_5_Core& gl)
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
    frame_angular_acceleration_location_ = -1;
    frame_inertia_scale_location_ = -1;
    dt_ = 0.0f;
    inverse_dt_ = 0.0f;
}
