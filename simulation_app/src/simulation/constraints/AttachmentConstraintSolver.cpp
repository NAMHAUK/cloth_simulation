#include "simulation/constraints/AttachmentConstraintSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint attachment_indices_binding = 1;
constexpr GLuint attachment_barycentric_offsets_binding = 2;
constexpr GLuint body_triangle_geometry_binding = 3;
constexpr std::uint32_t attachment_constraint_local_size = 128;

bool has_attachment_constraints(const AttachmentConstraintBufferView& constraint_view)
{
    return constraint_view.constraint_count > 0 &&
           constraint_view.ranges != nullptr &&
           !constraint_view.ranges->empty();
}

bool is_valid_attachment_constraint_view(const AttachmentConstraintBufferView& constraint_view)
{
    return constraint_view.attachment_index_buffer != 0 &&
           constraint_view.barycentric_offset_buffer != 0 &&
           has_attachment_constraints(constraint_view);
}
}

AttachmentConstraintSolver::AttachmentConstraintSolver(float stiffness) : stiffness_(stiffness)
{}

bool AttachmentConstraintSolver::is_initialized() const
{
    return program_ != 0;
}

bool AttachmentConstraintSolver::initialize(const std::filesystem::path& shader_path,
                                            QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Attachment constraint", gl);
    if (program_ == 0) {
        return false;
    }

    constraint_offset_location_ = gl.glGetUniformLocation(program_, "uConstraintOffset");
    constraint_count_location_ = gl.glGetUniformLocation(program_, "uConstraintCount");
    stiffness_location_ = gl.glGetUniformLocation(program_, "uStiffness");

    if (constraint_offset_location_ < 0 || constraint_count_location_ < 0 || stiffness_location_ < 0) {
        std::cerr << "Attachment constraint compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

bool AttachmentConstraintSolver::can_solve(const ClothMotionBufferView& motion_view,
                                           const AttachmentConstraintBufferView& constraint_view,
                                           const TriangleGeometryResources& body_triangle_geometry) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_attachment_constraint_view(constraint_view) &&
           is_valid_triangle_geometry_resource(body_triangle_geometry) &&
           stiffness_ > 0.0f;
}

void AttachmentConstraintSolver::solve(const ClothMotionBufferView& motion_view,
                                       const AttachmentConstraintBufferView& constraint_view,
                                       const TriangleGeometryResources& body_triangle_geometry,
                                       QOpenGLFunctions_4_5_Core& gl) const
{
    if (!has_attachment_constraints(constraint_view)) {
        return;
    }

    assert(can_solve(motion_view, constraint_view, body_triangle_geometry));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        attachment_indices_binding,
                        constraint_view.attachment_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        attachment_barycentric_offsets_binding,
                        constraint_view.barycentric_offset_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_geometry_binding,
                        body_triangle_geometry.triangle_geometry_buffer);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness_, 0.0f, 1.0f));

    for (const ElementRange& range : *constraint_view.ranges) {
        if (range.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, constraint_offset_location_, range.offset);
        gl.glProgramUniform1ui(program_, constraint_count_location_, range.count);
        gl.glDispatchCompute(compute_group_count(range.count, attachment_constraint_local_size), 1, 1);
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
