#include "simulation/collision/GarmentPrefitSolver.h"

#include "gpu/scene/SimulationGpuView.h"
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

bool GarmentPrefitSolver::can_solve(const SimulationGpuView& views, const ElementRange& vertex_range) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           vertex_range.count != 0u &&
           vertex_range.offset <= views.cloth_motion.vertex_count &&
           vertex_range.count <= views.cloth_motion.vertex_count - vertex_range.offset &&
           is_valid_triangle_geometry_resource(views.body_triangle_geometry) &&
           is_valid_triangle_bvh_resource(views.body_triangle_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const SimulationGpuView& views,
                                const ElementRange& vertex_range,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views, vertex_range));

    const auto& motion_view = views.cloth_motion;
    const auto& body_triangle_geometry = views.body_triangle_geometry;
    const auto& body_triangle_bvh = views.body_triangle_bvh;

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
