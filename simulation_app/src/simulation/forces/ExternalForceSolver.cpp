#include "simulation/forces/ExternalForceSolver.h"

#include "gpu/cloth/ClothGpuResources.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint velocities_binding = 2;
constexpr GLuint collision_states_binding = 3;
constexpr GLuint contact_normals_binding = 4;
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
    max_contacts_per_vertex_location_ = gl.glGetUniformLocation(program_, "uMaxContactsPerVertex");
    delta_time_location_ = gl.glGetUniformLocation(program_, "uDeltaTime");
    external_acceleration_location_ = gl.glGetUniformLocation(program_, "uExternalAcceleration");
    velocity_damping_location_ = gl.glGetUniformLocation(program_, "uVelocityDamping");

    if (vertex_count_location_ < 0 ||
        max_contacts_per_vertex_location_ < 0 ||
        delta_time_location_ < 0 ||
        external_acceleration_location_ < 0 ||
        velocity_damping_location_ < 0) {
        std::cerr << "Cloth external force compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

// 외부 힘 계산 -> 힘에 따른 위치 변화 GPU에서 갱신
void ExternalForceSolver::solve(const ClothMotionBufferView& motion_view,
                                const ClothCollisionStateBufferView& collision_view,
                                float dt,
                                const glm::vec3& external_acceleration,
                                float velocity_damping,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        !is_valid_motion_view(motion_view) ||
        !is_valid_collision_state_view(collision_view) ||
        motion_view.vertex_count != collision_view.vertex_count ||
        dt <= 0.0f) {
        return;
    }

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, velocities_binding, motion_view.velocity_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, collision_states_binding, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, contact_normals_binding, collision_view.contact_normal_buffer);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1ui(program_, max_contacts_per_vertex_location_, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1f(program_, delta_time_location_, dt);
    gl.glProgramUniform3f(program_,
                          external_acceleration_location_,
                          external_acceleration.x,
                          external_acceleration.y,
                          external_acceleration.z);
    gl.glProgramUniform1f(program_, velocity_damping_location_, velocity_damping);

    // shader가 외부 가속도에 따른 위치 변화량 계산 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, external_force_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void ExternalForceSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_count_location_ = -1;
    max_contacts_per_vertex_location_ = -1;
    delta_time_location_ = -1;
    external_acceleration_location_ = -1;
    velocity_damping_location_ = -1;
}
