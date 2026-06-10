#include "simulation/constraints/BendingConstraintSolver.h"

#include "gpu/cloth/ClothGpuResources.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint edge_indices_binding = 1;
constexpr GLuint rest_lengths_binding = 2;
constexpr std::uint32_t bending_constraint_local_size = 128;
}

bool BendingConstraintSolver::is_initialized() const
{
    return program_ != 0;
}

bool BendingConstraintSolver::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Bending constraint", gl);
    if (program_ == 0) {
        return false;
    }

    constraint_offset_location_ = gl.glGetUniformLocation(program_, "uConstraintOffset");
    constraint_count_location_ = gl.glGetUniformLocation(program_, "uConstraintCount");
    stiffness_location_ = gl.glGetUniformLocation(program_, "uStiffness");

    if (constraint_offset_location_ < 0 || constraint_count_location_ < 0 || stiffness_location_ < 0) {
        std::cerr << "Bending constraint compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

bool BendingConstraintSolver::can_solve(const ClothPositionBufferView& position_view,
                                        const DistanceConstraintBufferView& constraint_view,
                                        float stiffness) const
{
    return is_initialized() &&
           is_valid_position_view(position_view) &&
           is_valid_distance_constraint_view(constraint_view) &&
           stiffness > 0.0f;
}

void BendingConstraintSolver::solve(const ClothPositionBufferView& position_view,
                                    const DistanceConstraintBufferView& constraint_view,
                                    float stiffness,
                                    QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view, constraint_view, stiffness));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, edge_indices_binding, constraint_view.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, rest_lengths_binding, constraint_view.rest_length_buffer);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness, 0.0f, 1.0f));

    for (const ConstraintRange& range : *constraint_view.color_ranges) {
        if (range.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, constraint_offset_location_, range.offset);
        gl.glProgramUniform1ui(program_, constraint_count_location_, range.count);
        gl.glDispatchCompute(compute_group_count(range.count, bending_constraint_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }
}

void BendingConstraintSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    constraint_offset_location_ = -1;
    constraint_count_location_ = -1;
    stiffness_location_ = -1;
}
