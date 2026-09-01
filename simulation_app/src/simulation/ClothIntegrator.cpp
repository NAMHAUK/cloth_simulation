#include "simulation/ClothIntegrator.h"

#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "utils/ShaderUtils.h"

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;

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

void ClothIntegrator::initialize(const std::filesystem::path& shader_dir,
                                 float dt,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    if (dt <= 0.0f) {
        throw std::runtime_error("Cannot initialize cloth integrator with a non-positive time step.");
    }

    program_ = load_compute_program(shader_dir / "cloth" / "integrate_cloth.comp", gl);

    vertex_offset_loc_ = require_uniform_location(program_, "uVertexOffset", gl);
    vertex_count_loc_ = require_uniform_location(program_, "uVertexCount", gl);
    frame_start_position_loc_ = require_uniform_location(program_, "uFrameStartPosition", gl);
    frame_end_position_loc_ = require_uniform_location(program_, "uFrameEndPosition", gl);
    frame_rotation_delta_loc_ = require_uniform_location(program_, "uFrameRotationDelta", gl);
    frame_start_velocity_loc_ = require_uniform_location(program_, "uFrameStartVelocity", gl);
    frame_acceleration_loc_ = require_uniform_location(program_, "uFrameAcceleration", gl);
    frame_start_angular_velocity_loc_ = require_uniform_location(program_, "uFrameStartAngularVelocity", gl);
    frame_angular_acceleration_loc_ = require_uniform_location(program_, "uFrameAngularAcceleration", gl);

    const GLint delta_time_loc = require_uniform_location(program_, "uDeltaTime", gl);
    const GLint inverse_delta_time_loc = require_uniform_location(program_, "uInverseDeltaTime", gl);
    const GLint external_acceleration_loc = require_uniform_location(program_, "uExternalAcceleration", gl);
    const GLint velocity_damping_loc = require_uniform_location(program_, "uVelocityDamping", gl);
    const GLint frame_inertia_scale_loc = require_uniform_location(program_, "uFrameInertiaScale", gl);

    gl.glProgramUniform1f(program_, delta_time_loc, dt);
    gl.glProgramUniform1f(program_, inverse_delta_time_loc, 1.0f / dt);
    gl.glProgramUniform3f(program_, external_acceleration_loc, 0.0f, gravity_, 0.0f);
    gl.glProgramUniform1f(program_, velocity_damping_loc, velocity_damping_);
    gl.glProgramUniform1f(program_, frame_inertia_scale_loc, reference_frame_inertia_scale_);
}

void ClothIntegrator::integrate(const ClothGpuState& cloth_state,
                                GarmentLayer layer,
                                const ReferenceFrameKinematics& reference_frame_kinematics,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& garment_state = cloth_state.garment_buffer_states()[layer];
    const glm::vec3 frame_acceleration =
        clamp_vector_length(reference_frame_kinematics.acceleration, reference_frame_max_acceleration_);
    const glm::vec3 frame_angular_acceleration =
        clamp_vector_length(reference_frame_kinematics.angular_acceleration,
                            reference_frame_max_angular_acceleration_);

    gl.glUseProgram(program_);

    gl.glProgramUniform1ui(program_, vertex_offset_loc_, garment_state.vertex_start_index);
    gl.glProgramUniform1ui(program_, vertex_count_loc_, garment_state.vertex_count);
    gl.glProgramUniform3f(program_,
                          frame_start_position_loc_,
                          reference_frame_kinematics.start_position.x,
                          reference_frame_kinematics.start_position.y,
                          reference_frame_kinematics.start_position.z);
    gl.glProgramUniform3f(program_,
                          frame_end_position_loc_,
                          reference_frame_kinematics.end_position.x,
                          reference_frame_kinematics.end_position.y,
                          reference_frame_kinematics.end_position.z);
    gl.glProgramUniformMatrix3fv(program_,
                                 frame_rotation_delta_loc_,
                                 1,
                                 GL_FALSE,
                                 glm::value_ptr(reference_frame_kinematics.rotation_delta));
    gl.glProgramUniform3f(program_,
                          frame_start_velocity_loc_,
                          reference_frame_kinematics.start_velocity.x,
                          reference_frame_kinematics.start_velocity.y,
                          reference_frame_kinematics.start_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_acceleration_loc_,
                          frame_acceleration.x,
                          frame_acceleration.y,
                          frame_acceleration.z);
    gl.glProgramUniform3f(program_,
                          frame_start_angular_velocity_loc_,
                          reference_frame_kinematics.start_angular_velocity.x,
                          reference_frame_kinematics.start_angular_velocity.y,
                          reference_frame_kinematics.start_angular_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_angular_acceleration_loc_,
                          frame_angular_acceleration.x,
                          frame_angular_acceleration.y,
                          frame_angular_acceleration.z);

    gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothIntegrator::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_loc_ = -1;
    vertex_count_loc_ = -1;
    frame_start_position_loc_ = -1;
    frame_end_position_loc_ = -1;
    frame_rotation_delta_loc_ = -1;
    frame_start_velocity_loc_ = -1;
    frame_acceleration_loc_ = -1;
    frame_start_angular_velocity_loc_ = -1;
    frame_angular_acceleration_loc_ = -1;
}
