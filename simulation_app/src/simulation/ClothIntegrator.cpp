#include "simulation/ClothIntegrator.h"

#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "simulation/SimulationParams.h"
#include "utils/ShaderUtils.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;

}

ClothIntegrator::ClothIntegrator(const ClothIntegrationParams& params)
    : gravity_(params.gravity),
      velocity_damping_(params.velocity_damping),
      frame_inertia_scale_(params.reference_frame_inertia_scale)
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
    frame_start_linear_velocity_loc_ = require_uniform_location(program_, "uFrameStartLinearVelocity", gl);
    frame_linear_acceleration_loc_ = require_uniform_location(program_, "uFrameLinearAcceleration", gl);
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
    gl.glProgramUniform1f(program_, frame_inertia_scale_loc, frame_inertia_scale_);
}

void ClothIntegrator::integrate(const ClothGpuState& cloth_state,
                                const SceneState& scene,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);

    for (const GarmentObject& garment : scene.garments()) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];
        const auto& kinematics = scene.reference_frame_kinematics(garment.mesh.garment_category);
        integrate_garment(garment_state, kinematics, gl);
    }
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothIntegrator::integrate_garment(const GarmentBufferState& garment_state,
                                        const ReferenceFrameKinematics& kinematics,
                                        QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glProgramUniform1ui(program_, vertex_offset_loc_, garment_state.vertex_start_index);
    gl.glProgramUniform1ui(program_, vertex_count_loc_, garment_state.vertex_count);
    gl.glProgramUniform3f(program_,
                          frame_start_position_loc_,
                          kinematics.current_position.x,
                          kinematics.current_position.y,
                          kinematics.current_position.z);
    gl.glProgramUniform3f(program_,
                          frame_end_position_loc_,
                          kinematics.next_position.x,
                          kinematics.next_position.y,
                          kinematics.next_position.z);
    gl.glProgramUniformMatrix3fv(program_,
                                 frame_rotation_delta_loc_,
                                 1,
                                 GL_FALSE,
                                 glm::value_ptr(kinematics.rotation_delta));
    gl.glProgramUniform3f(program_,
                          frame_start_linear_velocity_loc_,
                          kinematics.current_linear_velocity.x,
                          kinematics.current_linear_velocity.y,
                          kinematics.current_linear_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_linear_acceleration_loc_,
                          kinematics.linear_acceleration.x,
                          kinematics.linear_acceleration.y,
                          kinematics.linear_acceleration.z);
    gl.glProgramUniform3f(program_,
                          frame_start_angular_velocity_loc_,
                          kinematics.current_angular_velocity.x,
                          kinematics.current_angular_velocity.y,
                          kinematics.current_angular_velocity.z);
    gl.glProgramUniform3f(program_,
                          frame_angular_acceleration_loc_,
                          kinematics.angular_acceleration.x,
                          kinematics.angular_acceleration.y,
                          kinematics.angular_acceleration.z);

    gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, local_size), 1, 1);
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
    frame_start_linear_velocity_loc_ = -1;
    frame_linear_acceleration_loc_ = -1;
    frame_start_angular_velocity_loc_ = -1;
    frame_angular_acceleration_loc_ = -1;
}
