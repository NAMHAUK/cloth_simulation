#include "simulation/collision/GarmentPrefitSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint character_triangle_geometry_binding = 2;
constexpr GLuint character_bvh_node_binding = 3;
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

    cloth_vertex_count_location_ = gl.glGetUniformLocation(program_, "uClothVertexCount");
    search_radius_location_ = gl.glGetUniformLocation(program_, "uSearchRadius");
    pushout_margin_location_ = gl.glGetUniformLocation(program_, "uPushoutMargin");

    if (cloth_vertex_count_location_ < 0 ||
        search_radius_location_ < 0 ||
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
                                    const TriangleGeometryResources& character_geometry,
                                    const TriangleBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_triangle_bvh_resource(character_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const ClothMotionBufferView& motion_view,
                                const TriangleGeometryResources& character_geometry,
                                const TriangleBvhResources& character_bvh,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, character_geometry, character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1f(program_, search_radius_location_, search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_location_, pushout_margin_);

    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, garment_prefit_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    cloth_vertex_count_location_ = -1;
    search_radius_location_ = -1;
    pushout_margin_location_ = -1;
    search_radius_ = 0.0f;
    pushout_margin_ = 0.0f;
}
