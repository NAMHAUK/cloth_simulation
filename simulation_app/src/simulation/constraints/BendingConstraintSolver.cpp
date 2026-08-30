#include "simulation/constraints/BendingConstraintSolver.h"

#include "gpu/cloth/ClothGpuState.h"
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

bool BendingConstraintSolver::can_solve(const ClothGpuState& cloth_state) const
{
    const ClothBufferElementCounts& counts = cloth_state.element_counts();
    return is_initialized() &&
           has_valid_distance_constraints(counts.bending_constraint, cloth_state.bending_color_states()) &&
           stiffness_ > 0.0f;
}

void BendingConstraintSolver::solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(cloth_state));

    gl.glUseProgram(program_);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness_, 0.0f, 1.0f));

    for (const ConstraintColorState& color_state : cloth_state.bending_color_states()) {
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
