#include "simulation/collision/GarmentPrefitSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint body_triangle_geometry_binding = 2;
constexpr GLuint body_triangle_bvh_node_binding = 3;
constexpr std::uint32_t garment_prefit_local_size = 128;
}

bool GarmentPrefitSolver::is_initialized() const
{
    return program_ != 0;
}

bool GarmentPrefitSolver::initialize(const std::filesystem::path& shader_path,
                                     float search_radius,
                                     float pushout_margin,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Garment pre-fit", gl);
    if (program_ == 0) {
        return false;
    }

    vertex_offset_location_ = gl.glGetUniformLocation(program_, "uVertexOffset");
    vertex_count_location_ = gl.glGetUniformLocation(program_, "uVertexCount");
    search_radius_squared_location_ = gl.glGetUniformLocation(program_, "uSearchRadiusSquared");
    pushout_margin_location_ = gl.glGetUniformLocation(program_, "uPushoutMargin");

    if (vertex_offset_location_ < 0 ||
        vertex_count_location_ < 0 ||
        search_radius_squared_location_ < 0 ||
        pushout_margin_location_ < 0) {
        std::cerr << "Garment pre-fit compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    search_radius_ = search_radius;
    pushout_margin_ = pushout_margin;
    return true;
}

bool GarmentPrefitSolver::can_solve(const ClothMotionBufferView& motion_view,
                                    const GarmentBufferRanges& garment_range,
                                    const TriangleGeometryResources& body_triangle_geometry,
                                    const TriangleBvhResources& body_triangle_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           garment_range.vertex_count != 0u &&
           garment_range.vertex_offset <= motion_view.vertex_count &&
           garment_range.vertex_count <= motion_view.vertex_count - garment_range.vertex_offset &&
           is_valid_triangle_geometry_resource(body_triangle_geometry) &&
           is_valid_triangle_bvh_resource(body_triangle_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const ClothMotionBufferView& motion_view,
                                const GarmentBufferRanges& garment_range,
                                const TriangleGeometryResources& body_triangle_geometry,
                                const TriangleBvhResources& body_triangle_bvh,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, garment_range, body_triangle_geometry, body_triangle_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_geometry_binding, body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_bvh_node_binding, body_triangle_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, vertex_offset_location_, garment_range.vertex_offset);
    gl.glProgramUniform1ui(program_, vertex_count_location_, garment_range.vertex_count);
    gl.glProgramUniform1f(program_, search_radius_squared_location_, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_location_, pushout_margin_);

    gl.glDispatchCompute(compute_group_count(garment_range.vertex_count, garment_prefit_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_location_ = -1;
    vertex_count_location_ = -1;
    search_radius_squared_location_ = -1;
    pushout_margin_location_ = -1;
    search_radius_ = 0.0f;
    pushout_margin_ = 0.0f;
}
