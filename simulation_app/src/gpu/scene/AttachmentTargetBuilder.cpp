#include "gpu/scene/AttachmentTargetBuilder.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cstdint>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint attachment_indices_binding = 1;
constexpr GLuint attachment_barycentric_offsets_binding = 2;
constexpr GLuint body_triangle_geometry_binding = 3;
constexpr GLuint body_triangle_bvh_node_binding = 4;
constexpr std::uint32_t attachment_target_local_size = 128;
constexpr float attachment_surface_offset = 0.005f;

bool is_valid_attachment_target_range(const AttachmentConstraintBufferView& attachment_view,
                                      const ElementRange& target_range)
{
    return attachment_view.attachment_index_buffer != 0 &&
           attachment_view.barycentric_offset_buffer != 0 &&
           target_range.count > 0u &&
           target_range.offset <= attachment_view.constraint_count &&
           target_range.count <= attachment_view.constraint_count - target_range.offset;
}
}

bool AttachmentTargetBuilder::is_initialized() const
{
    return program_ != 0;
}

bool AttachmentTargetBuilder::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Attachment target build", gl);
    if (program_ == 0) {
        return false;
    }

    constraint_offset_location_ = gl.glGetUniformLocation(program_, "uConstraintOffset");
    constraint_count_location_ = gl.glGetUniformLocation(program_, "uConstraintCount");
    surface_offset_location_ = gl.glGetUniformLocation(program_, "uSurfaceOffset");

    if (constraint_offset_location_ < 0 ||
        constraint_count_location_ < 0 ||
        surface_offset_location_ < 0) {
        std::cerr << "Attachment target build compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

bool AttachmentTargetBuilder::can_build(const ClothMotionBufferView& motion_view,
                                        const AttachmentConstraintBufferView& attachment_view,
                                        const ElementRange& target_range,
                                        const TriangleGeometryResources& body_triangle_geometry,
                                        const TriangleBvhResources& body_triangle_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_attachment_target_range(attachment_view, target_range) &&
           is_valid_triangle_geometry_resource(body_triangle_geometry) &&
           is_valid_triangle_bvh_resource(body_triangle_bvh);
}

bool AttachmentTargetBuilder::build(const ClothMotionBufferView& motion_view,
                                    const AttachmentConstraintBufferView& attachment_view,
                                    const ElementRange& target_range,
                                    const TriangleGeometryResources& body_triangle_geometry,
                                    const TriangleBvhResources& body_triangle_bvh,
                                    QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_build(motion_view, attachment_view, target_range, body_triangle_geometry, body_triangle_bvh)) {
        return false;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, attachment_indices_binding, attachment_view.attachment_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, attachment_barycentric_offsets_binding, attachment_view.barycentric_offset_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_geometry_binding, body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_bvh_node_binding, body_triangle_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, constraint_offset_location_, target_range.offset);
    gl.glProgramUniform1ui(program_, constraint_count_location_, target_range.count);
    gl.glProgramUniform1f(program_, surface_offset_location_, attachment_surface_offset);

    gl.glDispatchCompute(compute_group_count(target_range.count, attachment_target_local_size), 1, 1);
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
