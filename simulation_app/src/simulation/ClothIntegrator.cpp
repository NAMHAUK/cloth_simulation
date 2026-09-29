#include "simulation/ClothIntegrator.h"

#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "simulation/SimulationParams.h"
#include "utils/ShaderUtils.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_cross_product.hpp>
#include <glm/mat3x3.hpp>

#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;
}

struct IntegrationCoefficients
{
    glm::mat3 displacement;
    glm::mat3 position;
    glm::vec3 position_offset;
};

ClothIntegrator::ClothIntegrator(const ClothIntegrationParams& params)
    : gravity_(0.0f, params.gravity, 0.0f),
      damping_(params.velocity_damping),
      frame_inertia_scale_(params.reference_frame_inertia_scale)
{}

void ClothIntegrator::initialize(const std::filesystem::path& shader_dir,
                                 float substep_dt,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    if (substep_dt <= 0.0f) {
        throw std::runtime_error("Cannot initialize cloth integrator with a non-positive time step.");
    }

    program_ = load_compute_program(shader_dir / "cloth" / "integrate_cloth.comp", gl);

    vertex_offset_loc_ = require_uniform_location(program_, "uVertexOffset", gl);
    vertex_count_loc_ = require_uniform_location(program_, "uVertexCount", gl);

    displacement_coefficient_loc_ = require_uniform_location(program_, "uDisplacementCoefficient", gl);
    position_coefficient_loc_ = require_uniform_location(program_, "uPositionCoefficient", gl);
    position_offset_loc_ = require_uniform_location(program_, "uPositionOffset", gl);

    dt_ = substep_dt;
}

void ClothIntegrator::integrate(const ClothGpuState& cloth_state,
                                const SceneState& scene,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);

    for (const GarmentObject& garment : scene.garments()) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];
        const auto& reference_frame = scene.reference_frame_kinematics(garment.mesh.garment_category);
        const auto coefficients = integration_coefficients(reference_frame);

        gl.glProgramUniform1ui(program_, vertex_offset_loc_, garment_state.vertex_start_index);
        gl.glProgramUniform1ui(program_, vertex_count_loc_, garment_state.vertex_count);

        gl.glProgramUniformMatrix3fv(program_,
                                     displacement_coefficient_loc_,
                                     1,
                                     GL_FALSE,
                                     glm::value_ptr(coefficients.displacement));
        gl.glProgramUniformMatrix3fv(program_,
                                     position_coefficient_loc_,
                                     1,
                                     GL_FALSE,
                                     glm::value_ptr(coefficients.position));
        gl.glProgramUniform3fv(program_,
                               position_offset_loc_,
                               1,
                               glm::value_ptr(coefficients.position_offset));

        gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, local_size), 1, 1);
    }
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

IntegrationCoefficients ClothIntegrator::integration_coefficients(const ReferenceFrameKinematics& frame) const
{
    const auto angular_velocity_matrix = glm::matrixCross3(frame.current_angular_velocity);
    const auto angular_acceleration_matrix = glm::matrixCross3(frame.angular_acceleration);

    // 1. 변위에 반영할 damping & Coriolis 계수 행렬
    const auto coriolis_coefficient = 2.0f * dt_ * frame_inertia_scale_ * angular_velocity_matrix;
    const auto displacement_coefficients = frame.rotation_delta * (glm::mat3(damping_) - coriolis_coefficient);

    // 2. 현재 위치에 반영할 계수 행렬
    const auto rotation_correction =
        (dt_ * dt_ * frame_inertia_scale_) *
        (angular_acceleration_matrix + angular_velocity_matrix * angular_velocity_matrix);

    const auto rotated_position_coefficient = frame.rotation_delta * (glm::mat3(1.0f) - rotation_correction);
    const auto angular_velocity_coefficient = displacement_coefficients * (dt_ * angular_velocity_matrix);
    const auto position_coefficients = rotated_position_coefficient - angular_velocity_coefficient;

    // 3. 위치 보정 벡터
    const auto acceleration = gravity_ - frame_inertia_scale_ * frame.linear_acceleration;
    const auto frame_position_delta = dt_ * frame.current_linear_velocity;

    const auto frame_origin_offset = frame.next_position - position_coefficients * frame.current_position;
    const auto acceleration_offset = frame.rotation_delta * (dt_ * dt_ * acceleration);
    const auto frame_linear_velocity_offset = displacement_coefficients * frame_position_delta;

    const auto position_offset = frame_origin_offset + acceleration_offset - frame_linear_velocity_offset;

    return {displacement_coefficients, position_coefficients, position_offset};
}

void ClothIntegrator::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_loc_ = -1;
    vertex_count_loc_ = -1;

    displacement_coefficient_loc_ = -1;
    position_coefficient_loc_ = -1;
    position_offset_loc_ = -1;
}
