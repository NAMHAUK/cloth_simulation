#include "simulation/collision/GroundCollisionSolver.h"

#include "gpu/scene/SimulationGpuView.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t ground_collision_local_size = 128;
}

GroundCollisionSolver::GroundCollisionSolver(const GroundCollisionParams& params)
    : floor_height_(params.height),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

bool GroundCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

void GroundCollisionSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "collision" / "ground.comp", gl);
    vertex_count_location_ = require_uniform_location(program_, "uVertexCount", gl);
    floor_height_location_ = require_uniform_location(program_, "uFloorHeight", gl);
    static_friction_location_ = require_uniform_location(program_, "uStaticFriction", gl);
    dynamic_friction_location_ = require_uniform_location(program_, "uDynamicFriction", gl);
}

bool GroundCollisionSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           views.cloth_topology.vertex_count != 0u &&
           dynamic_friction_ >= 0.0f &&
           static_friction_ >= dynamic_friction_;
}

void GroundCollisionSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));

    // shader & GPU 연결
    gl.glUseProgram(program_);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_count_location_, views.cloth_topology.vertex_count);
    gl.glProgramUniform1f(program_, floor_height_location_, floor_height_);
    gl.glProgramUniform1f(program_, static_friction_location_, static_friction_);
    gl.glProgramUniform1f(program_, dynamic_friction_location_, dynamic_friction_);

    // shader가 바닥과 충돌 처리 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(views.cloth_topology.vertex_count, ground_collision_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GroundCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_count_location_ = -1;
    floor_height_location_ = -1;
    static_friction_location_ = -1;
    dynamic_friction_location_ = -1;
}
