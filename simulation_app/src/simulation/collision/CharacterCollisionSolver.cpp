#include "simulation/collision/CharacterCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint character_triangle_geometry_binding = 2;
constexpr GLuint character_bvh_node_binding = 3;
constexpr std::uint32_t character_collision_local_size = 128;
}

bool CharacterCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool CharacterCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                          float search_radius,
                                          float collision_thickness,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character collision", gl);
    if (program_ == 0) {
        return false;
    }

    cloth_vertex_count_location_ = gl.glGetUniformLocation(program_, "uClothVertexCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    search_radius_location_ = gl.glGetUniformLocation(program_, "uSearchRadiusSquared");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (cloth_vertex_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
        search_radius_location_ < 0 ||
        collision_thickness_location_ < 0) {
        std::cerr << "Character collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    search_radius_ = search_radius;
    collision_thickness_ = collision_thickness;
    return true;
}

bool CharacterCollisionSolver::can_solve(const ClothPositionBufferView& position_view,
                                         const TriangleGeometryResources& character_geometry,
                                         const MeshBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_position_view(position_view) &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_mesh_bvh_resource(character_bvh) &&
           search_radius_ > 0.0f &&
           collision_thickness_ > 0.0f;
}

void CharacterCollisionSolver::solve(const ClothPositionBufferView& position_view,
                                     const TriangleGeometryResources& character_geometry,
                                     const MeshBvhResources& character_bvh,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view, character_geometry, character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, position_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform1ui(program_, root_node_index_location_, character_bvh.root_node_index);
    gl.glProgramUniform1f(program_, search_radius_location_, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness_);

    gl.glDispatchCompute(compute_group_count(position_view.vertex_count, character_collision_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void CharacterCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    cloth_vertex_count_location_ = -1;
    root_node_index_location_ = -1;
    search_radius_location_ = -1;
    collision_thickness_location_ = -1;
    search_radius_ = 0.0f;
    collision_thickness_ = 0.0f;
}
