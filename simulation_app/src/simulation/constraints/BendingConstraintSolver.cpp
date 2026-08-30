#include "simulation/constraints/BendingConstraintSolver.h"

#include "gpu/cloth/ClothGpuState.h"
#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t bending_constraint_local_size = 128;
}

BendingConstraintSolver::BendingConstraintSolver(float stiffness) : stiffness_(stiffness)
{}

bool BendingConstraintSolver::is_initialized() const
{
    return program_ != 0;
}

void BendingConstraintSolver::initialize(const std::filesystem::path& shader_dir,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "constraints" / "bending.comp", gl);
    constraint_offset_location_ = require_uniform_location(program_, "uConstraintOffset", gl);
    constraint_count_location_ = require_uniform_location(program_, "uConstraintCount", gl);
    stiffness_location_ = require_uniform_location(program_, "uStiffness", gl);
}

bool BendingConstraintSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_distance_constraint_view(views.bending_constraints) &&
           stiffness_ > 0.0f;
}

void BendingConstraintSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));

    const auto& constraint_view = views.bending_constraints;

    gl.glUseProgram(program_);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness_, 0.0f, 1.0f));

    for (const ConstraintColorState& color_state : *constraint_view.color_states) {
        if (color_state.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, constraint_offset_location_, color_state.start_index);
        gl.glProgramUniform1ui(program_, constraint_count_location_, color_state.count);
        gl.glDispatchCompute(compute_group_count(color_state.count, bending_constraint_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void BendingConstraintSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    constraint_offset_location_ = -1;
    constraint_count_location_ = -1;
    stiffness_location_ = -1;
}
