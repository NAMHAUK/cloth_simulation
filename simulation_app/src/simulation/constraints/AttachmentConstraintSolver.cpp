#include "simulation/constraints/AttachmentConstraintSolver.h"

#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint attachment_indices_binding = 1;
constexpr GLuint attachment_barycentric_offsets_binding = 2;
constexpr GLuint body_triangle_positions_binding = 3;
constexpr GLuint body_triangle_normals_binding = 4;
constexpr std::uint32_t attachment_constraint_local_size = 128;

bool has_attachment_constraints(const std::array<GarmentBufferState, 2>& garments)
{
    return std::any_of(garments.begin(), garments.end(), [](const GarmentBufferState& garment_state) {
        return garment_state.active_attachment_constraint_count > 0u;
    });
}

bool is_valid_attachment_constraint_view(const AttachmentConstraintBufferView& constraint_view,
                                         const std::array<GarmentBufferState, 2>& garments)
{
    return constraint_view.attachment_index_buffer != 0 &&
           constraint_view.barycentric_offset_buffer != 0 &&
           constraint_view.constraint_count > 0 &&
           has_attachment_constraints(garments);
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
    constraint_offset_location_ = gl.glGetUniformLocation(program_, "uConstraintOffset");
    constraint_count_location_ = gl.glGetUniformLocation(program_, "uConstraintCount");
    stiffness_location_ = gl.glGetUniformLocation(program_, "uStiffness");

    if (constraint_offset_location_ < 0 || constraint_count_location_ < 0 || stiffness_location_ < 0) {
        throw std::runtime_error("Attachment constraint compute shader missing required uniforms.");
    }
}

bool AttachmentConstraintSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_attachment_constraint_view(views.attachment_constraints, views.garment_buffer_states) &&
           is_valid_body_triangle_resource(views.body_triangles) &&
           stiffness_ > 0.0f;
}

void AttachmentConstraintSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& constraint_view = views.attachment_constraints;
    if (!has_attachment_constraints(views.garment_buffer_states)) {
        return;
    }

    assert(can_solve(views));

    const auto& motion_view = views.cloth_motion;
    const auto& body_triangles = views.body_triangles;
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
                        body_triangle_positions_binding,
                        body_triangles.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_normals_binding,
                        body_triangles.normal_buffer);
    gl.glProgramUniform1f(program_, stiffness_location_, std::clamp(stiffness_, 0.0f, 1.0f));

    for (const GarmentBufferState& garment_state : views.garment_buffer_states) {
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
