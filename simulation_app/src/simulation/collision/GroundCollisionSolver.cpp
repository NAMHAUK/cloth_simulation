#include "simulation/collision/GroundCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint collision_states_binding = 1;
constexpr GLuint contact_normals_binding = 2;
constexpr std::uint32_t ground_collision_local_size = 128;
}

bool GroundCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool GroundCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                       float floor_height,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Ground collision", gl);
    if (program_ == 0) {
        return false;
    }

    vertex_count_location_ = gl.glGetUniformLocation(program_, "uVertexCount");
    max_contacts_per_vertex_location_ = gl.glGetUniformLocation(program_, "uMaxContactsPerVertex");
    floor_height_location_ = gl.glGetUniformLocation(program_, "uFloorHeight");

    if (vertex_count_location_ < 0 ||
        max_contacts_per_vertex_location_ < 0 ||
        floor_height_location_ < 0) {
        std::cerr << "Ground collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    floor_height_ = floor_height;
    return true;
}

bool GroundCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                      const ClothCollisionStateBufferView& collision_view) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count;
}

void GroundCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                  const ClothCollisionStateBufferView& collision_view,
                                  QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, collision_view));

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, collision_states_binding, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, contact_normals_binding, collision_view.contact_normal_buffer);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1ui(program_, max_contacts_per_vertex_location_, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1f(program_, floor_height_location_, floor_height_);

    // shader가 바닥과 충돌 처리 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, ground_collision_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void GroundCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    vertex_count_location_ = -1;
    max_contacts_per_vertex_location_ = -1;
    floor_height_location_ = -1;
    floor_height_ = 0.0f;
}
