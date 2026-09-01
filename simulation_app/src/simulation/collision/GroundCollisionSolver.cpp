#include "simulation/collision/GroundCollisionSolver.h"

#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/ShaderUtils.h"

#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;
}

GroundCollisionSolver::GroundCollisionSolver(const GroundCollisionParams& params)
    : floor_height_(params.height),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

void GroundCollisionSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "collision" / "ground.comp", gl);
    vertex_count_loc_ = require_uniform_location(program_, "uVertexCount", gl);

    const GLint floor_height_loc = require_uniform_location(program_, "uFloorHeight", gl);
    const GLint static_friction_loc = require_uniform_location(program_, "uStaticFriction", gl);
    const GLint dynamic_friction_loc = require_uniform_location(program_, "uDynamicFriction", gl);

    gl.glProgramUniform1f(program_, floor_height_loc, floor_height_);
    gl.glProgramUniform1f(program_, static_friction_loc, static_friction_);
    gl.glProgramUniform1f(program_, dynamic_friction_loc, dynamic_friction_);
}

void GroundCollisionSolver::solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t vertex_count = cloth_state.element_counts().vertex;

    gl.glUseProgram(program_);
    gl.glProgramUniform1ui(program_, vertex_count_loc_, vertex_count);

    gl.glDispatchCompute(compute_group_count(vertex_count, local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GroundCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_count_loc_ = -1;
}
