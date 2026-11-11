#include "simulation/constraints/AttachmentConstraintSolver.h"
#include "gpu/cloth/PbdClothGpuState.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cstdint>

namespace {
constexpr std::uint32_t local_size = 128;
}

AttachmentConstraintSolver::AttachmentConstraintSolver(float stiffness) : stiffness_(stiffness)
{}

void AttachmentConstraintSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "constraints" / "attachment.comp", gl);
    constraint_offset_loc_ = require_uniform_location(program_, "uConstraintOffset", gl);
    constraint_count_loc_ = require_uniform_location(program_, "uConstraintCount", gl);

    const GLint stiffness_loc = require_uniform_location(program_, "uStiffness", gl);
    gl.glProgramUniform1f(program_, stiffness_loc, std::clamp(stiffness_, 0.0f, 1.0f));
}

void AttachmentConstraintSolver::solve(const PbdClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(program_);

    for (const AttachmentBufferState& attachment_state : cloth_state.attachment_states()) {
        const std::uint32_t attachment_count = attachment_state.active_count;
        if (attachment_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, constraint_offset_loc_, attachment_state.start_index);
        gl.glProgramUniform1ui(program_, constraint_count_loc_, attachment_count);
        gl.glDispatchCompute(compute_group_count(attachment_count, local_size), 1, 1);
    }
}

void AttachmentConstraintSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    constraint_offset_loc_ = -1;
    constraint_count_loc_ = -1;
}
