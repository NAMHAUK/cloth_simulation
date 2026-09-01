#include "simulation/constraints/AttachmentConstraintSolver.h"

#include "gpu/cloth/ClothGpuState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t attachment_constraint_local_size = 128;

bool has_attachment_constraints(const std::array<GarmentBufferState, 2>& garments)
{
    return std::any_of(garments.begin(), garments.end(), [](const GarmentBufferState& garment_state) {
        return garment_state.active_attachment_constraint_count > 0u;
    });
}

bool has_valid_attachment_ranges(const std::array<GarmentBufferState, 2>& garments)
{
    std::uint32_t total_constraint_count = 0u;
    for (const GarmentBufferState& garment_state : garments) {
        total_constraint_count += garment_state.attachment_constraint_count;
    }

    return std::all_of(garments.begin(), garments.end(), [total_constraint_count](const auto& garment_state) {
        return garment_state.active_attachment_constraint_count == 0u ||
               (garment_state.active_attachment_constraint_count <=
                    garment_state.attachment_constraint_count &&
                is_valid_buffer_access(garment_state.attachment_constraint_start_index,
                                       garment_state.active_attachment_constraint_count,
                                       total_constraint_count));
    });
}

}

AttachmentConstraintSolver::AttachmentConstraintSolver(float stiffness) : stiffness_(stiffness)
{}

bool AttachmentConstraintSolver::is_initialized() const
{
    return program_ != 0;
}

void AttachmentConstraintSolver::initialize(const std::filesystem::path& shader_dir,
                                            QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "constraints" / "attachment.comp", gl);
    constraint_offset_location_ = require_uniform_location(program_, "uConstraintOffset", gl);
    constraint_count_location_ = require_uniform_location(program_, "uConstraintCount", gl);
    stiffness_location_ = require_uniform_location(program_, "uStiffness", gl);
}

bool AttachmentConstraintSolver::can_solve(const ClothGpuState& cloth_state) const
{
    const auto& garment_states = cloth_state.garment_buffer_states();
    return is_initialized() &&
           has_attachment_constraints(garment_states) &&
           has_valid_attachment_ranges(garment_states) &&
           stiffness_ > 0.0f;
}

void AttachmentConstraintSolver::solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& garment_states = cloth_state.garment_buffer_states();
    if (!has_attachment_constraints(garment_states)) {
        return;
    }

    assert(can_solve(cloth_state));

    gl.glUseProgram(program_);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness_, 0.0f, 1.0f));

    for (const GarmentBufferState& garment_state : garment_states) {
        if (garment_state.active_attachment_constraint_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_,
                               constraint_offset_location_,
                               garment_state.attachment_constraint_start_index);
        gl.glProgramUniform1ui(program_,
                               constraint_count_location_,
                               garment_state.active_attachment_constraint_count);
        gl.glDispatchCompute(compute_group_count(garment_state.active_attachment_constraint_count,
                                                 attachment_constraint_local_size),
                             1,
                             1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void AttachmentConstraintSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    constraint_offset_location_ = -1;
    constraint_count_location_ = -1;
    stiffness_location_ = -1;
}
