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
    bvh_node_count_location_ = gl.glGetUniformLocation(program_, "uBvhNodeCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    search_radius_location_ = gl.glGetUniformLocation(program_, "uSearchRadius");
    pushout_margin_location_ = gl.glGetUniformLocation(program_, "uPushoutMargin");

    if (cloth_vertex_count_location_ < 0 ||
        bvh_node_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
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

bool GarmentPrefitSolver::can_solve(const ClothPositionBufferView& position_view,
                                    const TriangleGeometryResources& character_geometry,
                                    const MeshBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_position_view(position_view) &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_mesh_bvh_resource(character_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const ClothPositionBufferView& position_view,
                                const TriangleGeometryResources& character_geometry,
                                const MeshBvhResources& character_bvh,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view, character_geometry, character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform1ui(program_, bvh_node_count_location_, character_bvh.node_count);
    gl.glProgramUniform1ui(program_, root_node_index_location_, character_bvh.root_node_index);
    gl.glProgramUniform1f(program_, search_radius_location_, search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_location_, pushout_margin_);

    gl.glDispatchCompute(compute_group_count(position_view.vertex_count, garment_prefit_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    cloth_vertex_count_location_ = -1;
    bvh_node_count_location_ = -1;
    root_node_index_location_ = -1;
    search_radius_location_ = -1;
    pushout_margin_location_ = -1;
    search_radius_ = 0.0f;
    pushout_margin_ = 0.0f;
}
