#include "simulation/collision/GarmentPrefitSolver.h"

#include "simulation/SimulationParams.h"
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

GarmentPrefitSolver::GarmentPrefitSolver(const PrefitParams& params)
    : search_radius_(params.surface_search_radius),
      pushout_margin_(params.pushout_margin)
{}

bool GarmentPrefitSolver::is_initialized() const
{
    return program_ != 0;
}

bool GarmentPrefitSolver::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
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

    return true;
}

bool GarmentPrefitSolver::can_solve(const ClothMotionBufferView& motion_view,
                                    const ElementRange& vertex_range,
                                    const TriangleGeometryResources& body_triangle_geometry,
                                    const TriangleBvhResources& body_triangle_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           vertex_range.count != 0u &&
           vertex_range.offset <= motion_view.vertex_count &&
           vertex_range.count <= motion_view.vertex_count - vertex_range.offset &&
           is_valid_triangle_geometry_resource(body_triangle_geometry) &&
           is_valid_triangle_bvh_resource(body_triangle_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const ClothMotionBufferView& motion_view,
                                const ElementRange& vertex_range,
                                const TriangleGeometryResources& body_triangle_geometry,
                                const TriangleBvhResources& body_triangle_bvh,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, vertex_range, body_triangle_geometry, body_triangle_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_geometry_binding,
                        body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_bvh_node_binding,
                        body_triangle_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, vertex_offset_location_, vertex_range.offset);
    gl.glProgramUniform1ui(program_, vertex_count_location_, vertex_range.count);
    gl.glProgramUniform1f(program_, search_radius_squared_location_, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_location_, pushout_margin_);

    gl.glDispatchCompute(compute_group_count(vertex_range.count, garment_prefit_local_size), 1, 1);
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
}
