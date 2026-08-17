#include "gpu/scene/AttachmentTargetBuilder.h"

#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cstdint>
#include <stdexcept>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint attachment_indices_binding = 1;
constexpr GLuint attachment_barycentric_offsets_binding = 2;
constexpr GLuint body_triangle_geometry_binding = 3;
constexpr GLuint body_triangle_bvh_node_binding = 4;
constexpr std::uint32_t attachment_target_local_size = 128;

bool is_valid_attachment_target_access(const AttachmentConstraintBufferView& attachment_view,
                                       const GarmentBufferState& garment_state)
{
    return attachment_view.attachment_index_buffer != 0 &&
           attachment_view.barycentric_offset_buffer != 0 &&
           is_valid_buffer_access(garment_state.attachment_constraint_start_index,
                                  garment_state.attachment_constraint_count,
                                  attachment_view.constraint_count);
}
}

bool AttachmentTargetBuilder::is_initialized() const
{
    return program_ != 0;
}

void AttachmentTargetBuilder::initialize(const std::filesystem::path& shader_dir,
                                         float surface_offset,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "setup" / "garment_attachment_target_build.comp",
                                    "Attachment target build",
                                    gl);
    constraint_offset_location_ = gl.glGetUniformLocation(program_, "uConstraintOffset");
    constraint_count_location_ = gl.glGetUniformLocation(program_, "uConstraintCount");
    surface_offset_location_ = gl.glGetUniformLocation(program_, "uSurfaceOffset");

    if (constraint_offset_location_ < 0 || constraint_count_location_ < 0 || surface_offset_location_ < 0) {
        throw std::runtime_error("Attachment target build compute shader missing required uniforms.");
    }

    gl.glProgramUniform1f(program_, surface_offset_location_, surface_offset);
}

bool AttachmentTargetBuilder::can_build(const SimulationGpuView& views, GarmentLayer layer) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_attachment_target_access(views.attachment_constraints,
                                             views.garment_buffer_states[layer]) &&
           is_valid_triangle_geometry_resource(views.body_triangle_geometry) &&
           is_valid_triangle_bvh_resource(views.body_triangle_bvh);
}

bool AttachmentTargetBuilder::build(const SimulationGpuView& views,
                                    GarmentLayer layer,
                                    QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_build(views, layer)) {
        return false;
    }

    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
    const auto& motion_view = views.cloth_motion;
    const auto& attachment_view = views.attachment_constraints;
    const auto& body_triangle_geometry = views.body_triangle_geometry;
    const auto& body_triangle_bvh = views.body_triangle_bvh;
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        attachment_indices_binding,
                        attachment_view.attachment_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        attachment_barycentric_offsets_binding,
                        attachment_view.barycentric_offset_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_geometry_binding,
                        body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_bvh_node_binding,
                        body_triangle_bvh.node_buffer);

    gl.glProgramUniform1ui(program_,
                           constraint_offset_location_,
                           garment_state.attachment_constraint_start_index);
    gl.glProgramUniform1ui(program_, constraint_count_location_, garment_state.attachment_constraint_count);

    gl.glDispatchCompute(
        compute_group_count(garment_state.attachment_constraint_count, attachment_target_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    return true;
}

void AttachmentTargetBuilder::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    constraint_offset_location_ = -1;
    constraint_count_location_ = -1;
    surface_offset_location_ = -1;
}
