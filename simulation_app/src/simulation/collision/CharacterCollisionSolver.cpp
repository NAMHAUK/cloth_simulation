#include "simulation/collision/CharacterCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint character_positions_binding = 2;
constexpr GLuint character_indices_binding = 3;
constexpr std::uint32_t character_collision_local_size = 128;

bool is_valid_character_topology(const MeshTopologyResources& character_topology)
{
    return character_topology.position_buffer != 0 &&
           character_topology.index_buffer != 0 &&
           character_topology.vertex_count != 0 &&
           character_topology.triangle_count != 0;
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
    character_triangle_count_location_ = gl.glGetUniformLocation(program_, "uCharacterTriangleCount");
    character_position_component_offset_location_ = gl.glGetUniformLocation(program_, "uCharacterPositionComponentOffset");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (cloth_vertex_count_location_ < 0 ||
        character_triangle_count_location_ < 0 ||
        character_position_component_offset_location_ < 0 ||
        collision_thickness_location_ < 0) {
        std::cerr << "Character collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    return true;
}

bool CharacterCollisionSolver::can_solve(const ClothPositionBufferView& position_view,
                                         const MeshTopologyResources& character_topology) const
{
    return is_initialized() &&
           is_valid_position_view(position_view) &&
           is_valid_character_topology(character_topology) &&
           collision_thickness_ > 0.0f;
}

void CharacterCollisionSolver::solve(const ClothPositionBufferView& position_view,
                                     const MeshTopologyResources& character_topology,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(position_view, character_topology));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, position_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, position_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_positions_binding, character_topology.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_indices_binding, character_topology.index_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, position_view.vertex_count);
    gl.glProgramUniform1ui(program_, character_triangle_count_location_, character_topology.triangle_count);
    gl.glProgramUniform1ui(program_,
                           character_position_component_offset_location_,
                           character_topology.position_component_offset);
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
    character_triangle_count_location_ = -1;
    character_position_component_offset_location_ = -1;
    collision_thickness_location_ = -1;
    collision_thickness_ = 0.0f;
}
