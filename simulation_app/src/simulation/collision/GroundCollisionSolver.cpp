#include "simulation/collision/GroundCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
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
    floor_height_location_ = gl.glGetUniformLocation(program_, "uFloorHeight");

    if (vertex_count_location_ < 0 || floor_height_location_ < 0) {
        std::cerr << "Ground collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    floor_height_ = floor_height;
    return true;
}

bool GroundCollisionSolver::can_solve(const ClothPositionBufferView& position_view) const
{
    return is_initialized() && is_valid_position_view(position_view);
}

void GroundCollisionSolver::solve(const ClothPositionBufferView& position_view, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view));

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, position_view.previous_position_buffer);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform1f(program_, floor_height_location_, floor_height_);

    // shader가 바닥과 충돌 처리 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(position_view.vertex_count, ground_collision_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void GroundCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    vertex_count_location_ = -1;
    floor_height_location_ = -1;
    floor_height_ = 0.0f;
}
