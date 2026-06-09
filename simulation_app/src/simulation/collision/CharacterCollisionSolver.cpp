#include "simulation/collision/CharacterCollisionSolver.h"

#include "gpu/body/CharacterGpuResources.h"
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

bool is_valid_character_geometry(const CharacterTriangleGeometryResources& character_geometry)
{
    return character_geometry.triangle_geometry_buffer != 0 &&
           character_geometry.triangle_count != 0;
}

bool is_valid_character_bvh(const CharacterBvhResources& character_bvh)
{
    return character_bvh.node_buffer != 0 &&
           character_bvh.node_count != 0 &&
           character_bvh.root_node_index < character_bvh.node_count;
}
}

bool CharacterCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool CharacterCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                          float collision_thickness,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character collision", gl);
    if (program_ == 0) {
        return false;
    }

    cloth_vertex_count_location_ = gl.glGetUniformLocation(program_, "uClothVertexCount");
    bvh_node_count_location_ = gl.glGetUniformLocation(program_, "uBvhNodeCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (cloth_vertex_count_location_ < 0 ||
        bvh_node_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
        collision_thickness_location_ < 0) {
        std::cerr << "Character collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    return true;
}

bool CharacterCollisionSolver::can_solve(const ClothPositionBufferView& position_view,
                                         const CharacterTriangleGeometryResources& character_geometry,
                                         const CharacterBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_position_view(position_view) &&
           is_valid_character_geometry(character_geometry) &&
           is_valid_character_bvh(character_bvh) &&
           collision_thickness_ > 0.0f;
}

void CharacterCollisionSolver::solve(const ClothPositionBufferView& position_view,
                                     const CharacterTriangleGeometryResources& character_geometry,
                                     const CharacterBvhResources& character_bvh,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view, character_geometry, character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, position_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform1ui(program_, bvh_node_count_location_, character_bvh.node_count);
    gl.glProgramUniform1ui(program_, root_node_index_location_, character_bvh.root_node_index);
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
    bvh_node_count_location_ = -1;
    root_node_index_location_ = -1;
    collision_thickness_location_ = -1;
    collision_thickness_ = 0.0f;
}
