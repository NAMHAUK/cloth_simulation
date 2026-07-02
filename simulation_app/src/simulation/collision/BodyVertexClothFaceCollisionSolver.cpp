#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint cloth_current_positions_binding = 0;
constexpr GLuint cloth_previous_positions_binding = 1;
constexpr GLuint cloth_triangle_indices_binding = 2;
constexpr GLuint colorized_cloth_triangle_ids_binding = 3;
constexpr GLuint body_previous_positions_binding = 4;
constexpr GLuint body_current_positions_binding = 5;
constexpr GLuint body_vertex_normals_binding = 6;
constexpr GLuint body_triangle_indices_binding = 7;
constexpr GLuint body_bvh_nodes_binding = 8;
constexpr GLuint collision_states_binding = 9;
constexpr GLuint contact_normals_binding = 10;
constexpr std::uint32_t body_vertex_cloth_face_collision_local_size = 128;
}

bool BodyVertexClothFaceCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool BodyVertexClothFaceCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Body vertex/cloth face collision", gl);
    if (program_ == 0) {
        return false;
    }

    triangle_color_offset_location_ = gl.glGetUniformLocation(program_, "uTriangleColorOffset");
    triangle_dispatch_count_location_ = gl.glGetUniformLocation(program_, "uTriangleDispatchCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    max_contacts_per_vertex_location_ = gl.glGetUniformLocation(program_, "uMaxContactsPerVertex");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");
    max_correction_length_location_ = gl.glGetUniformLocation(program_, "uMaxCorrectionLength");

    if (triangle_color_offset_location_ < 0 ||
        triangle_dispatch_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
        max_contacts_per_vertex_location_ < 0 ||
        collision_thickness_location_ < 0 ||
        max_correction_length_location_ < 0) {
        std::cerr << "Body vertex/cloth face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
    return true;
}

bool BodyVertexClothFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionStateBufferView& collision_view,
                                                   const ClothMeshTopologyResources& cloth_topology,
                                                   const ClothTriangleColorView& triangle_color_view,
                                                   const CharacterVertexBufferView& character_vertex_view,
                                                   const CharacterMeshTopologyResources& character_topology,
                                                   const MeshBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           is_valid_cloth_triangle_color_view(triangle_color_view) &&
           cloth_topology.triangle_count == triangle_color_view.triangle_count &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_character_mesh_topology_resource(character_topology) &&
           is_valid_mesh_bvh_resource(character_bvh) &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void BodyVertexClothFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const ClothMeshTopologyResources& cloth_topology,
                                               const ClothTriangleColorView& triangle_color_view,
                                               const CharacterVertexBufferView& character_vertex_view,
                                               const CharacterMeshTopologyResources& character_topology,
                                               const MeshBvhResources& character_bvh,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view,
                     collision_view,
                     cloth_topology,
                     triangle_color_view,
                     character_vertex_view,
                     character_topology,
                     character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_previous_positions_binding, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_triangle_indices_binding, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, colorized_cloth_triangle_ids_binding, triangle_color_view.triangle_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_previous_positions_binding, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_current_positions_binding, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_normals_binding, character_vertex_view.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_triangle_indices_binding, character_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_bvh_nodes_binding, character_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, collision_states_binding, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, contact_normals_binding, collision_view.contact_normal_buffer);
    gl.glProgramUniform1ui(program_, root_node_index_location_, character_bvh.root_node_index);
    gl.glProgramUniform1ui(program_, max_contacts_per_vertex_location_, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness_);
    gl.glProgramUniform1f(program_, max_correction_length_location_, max_correction_length_);

    for (const ElementRange& range : *triangle_color_view.color_ranges) {
        if (range.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, triangle_color_offset_location_, range.offset);
        gl.glProgramUniform1ui(program_, triangle_dispatch_count_location_, range.count);
        gl.glDispatchCompute(compute_group_count(range.count, body_vertex_cloth_face_collision_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }
}

void BodyVertexClothFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    triangle_color_offset_location_ = -1;
    triangle_dispatch_count_location_ = -1;
    root_node_index_location_ = -1;
    max_contacts_per_vertex_location_ = -1;
    collision_thickness_location_ = -1;
    max_correction_length_location_ = -1;
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}
