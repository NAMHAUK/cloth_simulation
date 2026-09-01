#include "simulation/constraints/StretchConstraintSolver.h"

#include "gpu/cloth/ClothGpuState.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;
}

StretchConstraintSolver::StretchConstraintSolver(float stiffness) : stiffness_(stiffness)
{}

void StretchConstraintSolver::initialize(const std::filesystem::path& shader_dir,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "constraints" / "stretch.comp", gl);
    constraint_offset_loc_ = require_uniform_location(program_, "uConstraintOffset", gl);
    constraint_count_loc_ = require_uniform_location(program_, "uConstraintCount", gl);

    const GLint stiffness_loc = require_uniform_location(program_, "uStiffness", gl);
    gl.glProgramUniform1f(program_, stiffness_loc, std::clamp(stiffness_, 0.0f, 1.0f));
}

void StretchConstraintSolver::solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);

    for (const ConstraintColorState& color_state : cloth_state.stretch_color_states()) {
        if (color_state.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, constraint_offset_loc_, color_state.start_index);
        gl.glProgramUniform1ui(program_, constraint_count_loc_, color_state.count);
        gl.glDispatchCompute(compute_group_count(color_state.count, local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void StretchConstraintSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    constraint_offset_loc_ = -1;
    constraint_count_loc_ = -1;
}
