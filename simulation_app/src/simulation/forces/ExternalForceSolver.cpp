#include "simulation/forces/ExternalForceSolver.h"

#include "gpu/cloth/ClothGpuResources.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr std::uint32_t external_force_local_size = 128;
}

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
    vertex_count_location_ = gl.glGetUniformLocation(program_, "uVertexCount");
    acc_displacement_location_ = gl.glGetUniformLocation(program_, "uAccelerationDisplacement");
    velocity_damping_location_ = gl.glGetUniformLocation(program_, "uVelocityDamping");

    if (vertex_count_location_ < 0 || acc_displacement_location_ < 0 || velocity_damping_location_ < 0) {
        std::cerr << "Cloth external force compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

// 외부 힘 계산 -> 힘에 따른 위치 변화 GPU에서 갱신
void ExternalForceSolver::solve(const ClothPositionBufferView& position_view,
                                float dt,
                                const glm::vec3& external_acceleration,
                                float velocity_damping,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !is_valid_position_view(position_view) || dt <= 0.0f) {
        return;
    }

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, position_view.previous_position_buffer);

    // shader에 값 전달
    const glm::vec3 acceleration_displacement = external_acceleration * (dt * dt);
    gl.glProgramUniform1ui(program_, vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform3f(program_,
                          acc_displacement_location_,
                          acceleration_displacement.x,
                          acceleration_displacement.y,
                          acceleration_displacement.z);
    gl.glProgramUniform1f(program_, velocity_damping_location_, velocity_damping);

    // shader가 외부 가속도에 따른 위치 변화량 계산 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(position_view.vertex_count, external_force_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void ExternalForceSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    vertex_count_location_ = -1;
    acc_displacement_location_ = -1;
    velocity_damping_location_ = -1;
}
