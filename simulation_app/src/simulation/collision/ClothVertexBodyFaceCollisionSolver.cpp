#include "simulation/collision/ClothVertexBodyFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint character_triangle_geometry_binding = 2;
constexpr GLuint character_bvh_node_binding = 3;
constexpr GLuint collision_states_binding = 4;
constexpr GLuint contact_normals_binding = 5;
constexpr std::uint32_t cloth_vertex_body_face_collision_local_size = 128;
}

bool ClothVertexBodyFaceCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool ClothVertexBodyFaceCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Cloth vertex/body face collision", gl);
    if (program_ == 0) {
        return false;
    }

    cloth_vertex_count_location_ = gl.glGetUniformLocation(program_, "uClothVertexCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    max_contacts_per_vertex_location_ = gl.glGetUniformLocation(program_, "uMaxContactsPerVertex");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");
    max_correction_length_location_ = gl.glGetUniformLocation(program_, "uMaxCorrectionLength");

    if (cloth_vertex_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
        max_contacts_per_vertex_location_ < 0 ||
        collision_thickness_location_ < 0 ||
        max_correction_length_location_ < 0) {
        std::cerr << "Cloth vertex/body face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
    return true;
}

bool ClothVertexBodyFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionStateBufferView& collision_view,
                                                   const TriangleGeometryResources& character_geometry,
                                                   const MeshBvhResources& character_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_mesh_bvh_resource(character_bvh) &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void ClothVertexBodyFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const TriangleGeometryResources& character_geometry,
                                               const MeshBvhResources& character_bvh,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, collision_view, character_geometry, character_bvh));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, previous_positions_binding, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, collision_states_binding, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, contact_normals_binding, collision_view.contact_normal_buffer);

    gl.glProgramUniform1ui(program_, cloth_vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1ui(program_, root_node_index_location_, character_bvh.root_node_index);
    gl.glProgramUniform1ui(program_, max_contacts_per_vertex_location_, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness_);
    gl.glProgramUniform1f(program_, max_correction_length_location_, max_correction_length_);

    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, cloth_vertex_body_face_collision_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void ClothVertexBodyFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    cloth_vertex_count_location_ = -1;
    root_node_index_location_ = -1;
    max_contacts_per_vertex_location_ = -1;
    collision_thickness_location_ = -1;
    max_correction_length_location_ = -1;
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}
