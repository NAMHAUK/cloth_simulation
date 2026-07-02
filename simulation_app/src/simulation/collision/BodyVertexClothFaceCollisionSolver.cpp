#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <iostream>

namespace {
constexpr std::uint32_t pair_generate_local_size = 128;
constexpr std::uint32_t pair_accumulate_local_size = 128;
constexpr std::uint32_t pair_apply_local_size = 128;
constexpr std::uint32_t pair_capacity_multiplier = 32;
constexpr GLuint pair_generate_cloth_current_positions_binding = 0;
constexpr GLuint pair_generate_cloth_previous_positions_binding = 1;
constexpr GLuint pair_generate_cloth_triangle_indices_binding = 2;
constexpr GLuint pair_generate_body_vertex_ids_binding = 3;
constexpr GLuint pair_generate_body_vertex_bvh_nodes_binding = 4;
constexpr GLuint pair_generate_pair_records_binding = 5;
constexpr GLuint pair_generate_pair_count_binding = 6;
constexpr GLuint pair_accumulate_cloth_current_positions_binding = 0;
constexpr GLuint pair_accumulate_cloth_previous_positions_binding = 1;
constexpr GLuint pair_accumulate_cloth_triangle_indices_binding = 2;
constexpr GLuint pair_accumulate_body_previous_positions_binding = 3;
constexpr GLuint pair_accumulate_body_current_positions_binding = 4;
constexpr GLuint pair_accumulate_body_vertex_normals_binding = 5;
constexpr GLuint pair_accumulate_pair_records_binding = 6;
constexpr GLuint pair_accumulate_pair_count_binding = 7;
constexpr GLuint pair_accumulate_correction_sums_binding = 8;
constexpr GLuint pair_accumulate_correction_counts_binding = 9;
constexpr GLuint pair_accumulate_contact_candidate_counts_binding = 10;
constexpr GLuint pair_accumulate_contact_candidates_binding = 11;
constexpr GLuint pair_apply_cloth_current_positions_binding = 0;
constexpr GLuint pair_apply_collision_states_binding = 1;
constexpr GLuint pair_apply_contact_normals_binding = 2;
constexpr GLuint pair_apply_correction_sums_binding = 3;
constexpr GLuint pair_apply_correction_counts_binding = 4;
constexpr GLuint pair_apply_contact_candidate_counts_binding = 5;
constexpr GLuint pair_apply_contact_candidates_binding = 6;
#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
constexpr std::uint32_t gpu_timing_log_interval = 512u;
#endif
}

bool BodyVertexClothFaceCollisionSolver::is_initialized() const
{
    return has_pair_programs();
}

bool BodyVertexClothFaceCollisionSolver::initialize(const std::filesystem::path& pair_generate_shader_path,
                                                    const std::filesystem::path& pair_accumulate_shader_path,
                                                    const std::filesystem::path& pair_apply_shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    pair_generate_program_ = load_compute_program(pair_generate_shader_path,
                                                  "Body vertex/cloth face pair generation",
                                                  gl);
    pair_accumulate_program_ = load_compute_program(pair_accumulate_shader_path,
                                                    "Body vertex/cloth face pair accumulation",
                                                    gl);
    pair_apply_program_ = load_compute_program(pair_apply_shader_path,
                                               "Body vertex/cloth face pair apply",
                                               gl);
    if (!has_pair_programs()) {
        release(gl);
        return false;
    }

    pair_generate_triangle_count_location_ = gl.glGetUniformLocation(pair_generate_program_, "uTriangleCount");
    pair_generate_root_node_index_location_ = gl.glGetUniformLocation(pair_generate_program_, "uRootNodeIndex");
    pair_generate_max_pair_count_location_ = gl.glGetUniformLocation(pair_generate_program_, "uMaxPairCount");
    pair_accumulate_max_pair_count_location_ = gl.glGetUniformLocation(pair_accumulate_program_, "uMaxPairCount");
    pair_accumulate_collision_thickness_location_ =
        gl.glGetUniformLocation(pair_accumulate_program_, "uCollisionThickness");
    pair_accumulate_contact_candidate_capacity_location_ =
        gl.glGetUniformLocation(pair_accumulate_program_, "uContactCandidateCapacityPerVertex");
    pair_apply_vertex_count_location_ = gl.glGetUniformLocation(pair_apply_program_, "uVertexCount");
    pair_apply_max_contacts_per_vertex_location_ =
        gl.glGetUniformLocation(pair_apply_program_, "uMaxContactsPerVertex");
    pair_apply_contact_candidate_capacity_location_ =
        gl.glGetUniformLocation(pair_apply_program_, "uContactCandidateCapacityPerVertex");
    pair_apply_max_correction_length_location_ =
        gl.glGetUniformLocation(pair_apply_program_, "uMaxCorrectionLength");

    if (pair_generate_triangle_count_location_ < 0 ||
        pair_generate_root_node_index_location_ < 0 ||
        pair_generate_max_pair_count_location_ < 0 ||
        pair_accumulate_max_pair_count_location_ < 0 ||
        pair_accumulate_collision_thickness_location_ < 0 ||
        pair_accumulate_contact_candidate_capacity_location_ < 0 ||
        pair_apply_vertex_count_location_ < 0 ||
        pair_apply_max_contacts_per_vertex_location_ < 0 ||
        pair_apply_contact_candidate_capacity_location_ < 0 ||
        pair_apply_max_correction_length_location_ < 0) {
        std::cerr << "Body vertex/cloth face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    gpu_timer_.initialize("BodyVertexClothFaceCollisionSolver::solve", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool BodyVertexClothFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionStateBufferView& collision_view,
                                                   const ClothMeshTopologyResources& cloth_topology,
                                                   const CharacterVertexBufferView& character_vertex_view,
                                                   const BodyVertexBvhResources& body_vertex_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_body_vertex_bvh_resource(body_vertex_bvh) &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void BodyVertexClothFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const ClothMeshTopologyResources& cloth_topology,
                                               const CharacterVertexBufferView& character_vertex_view,
                                               const BodyVertexBvhResources& body_vertex_bvh,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view,
                     collision_view,
                     cloth_topology,
                     character_vertex_view,
                     body_vertex_bvh));

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    const bool gpu_timing_started = gpu_timer_.begin(gl);
#endif

    solve_pair_path(motion_view, collision_view, cloth_topology, character_vertex_view, body_vertex_bvh, gl);

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    if (gpu_timing_started) {
        gpu_timer_.end(gl);
    }
#endif
}

bool BodyVertexClothFaceCollisionSolver::solve_pair_path(const ClothMotionBufferView& motion_view,
                                                         const ClothCollisionStateBufferView& collision_view,
                                                         const ClothMeshTopologyResources& cloth_topology,
                                                         const CharacterVertexBufferView& character_vertex_view,
                                                         const BodyVertexBvhResources& body_vertex_bvh,
                                                         QOpenGLFunctions_4_5_Core& gl) const
{
    if (!ensure_pair_scratch_buffers(motion_view.vertex_count,
                                     cloth_topology.triangle_count,
                                     collision_view.max_contacts_per_vertex,
                                     gl)) {
        return false;
    }

    clear_pair_scratch_buffers(gl);

    gl.glUseProgram(pair_generate_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_cloth_current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_cloth_previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_cloth_triangle_indices_binding,
                        cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_body_vertex_ids_binding,
                        body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_body_vertex_bvh_nodes_binding,
                        body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_pair_records_binding,
                        pair_scratch_buffers_.pair_record);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_generate_pair_count_binding,
                        pair_scratch_buffers_.pair_count);
    gl.glProgramUniform1ui(pair_generate_program_,
                           pair_generate_triangle_count_location_,
                           cloth_topology.triangle_count);
    gl.glProgramUniform1ui(pair_generate_program_,
                           pair_generate_root_node_index_location_,
                           body_vertex_bvh.root_node_index);
    gl.glProgramUniform1ui(pair_generate_program_,
                           pair_generate_max_pair_count_location_,
                           pair_scratch_buffers_.pair_capacity);
    gl.glDispatchCompute(compute_group_count(cloth_topology.triangle_count, pair_generate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    gl.glUseProgram(pair_accumulate_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_cloth_current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_cloth_previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_cloth_triangle_indices_binding,
                        cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_body_previous_positions_binding,
                        character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_body_current_positions_binding,
                        character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_body_vertex_normals_binding,
                        character_vertex_view.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_pair_records_binding,
                        pair_scratch_buffers_.pair_record);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_pair_count_binding,
                        pair_scratch_buffers_.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_correction_sums_binding,
                        pair_scratch_buffers_.correction_sum);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_correction_counts_binding,
                        pair_scratch_buffers_.correction_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_contact_candidate_counts_binding,
                        pair_scratch_buffers_.contact_candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_accumulate_contact_candidates_binding,
                        pair_scratch_buffers_.contact_candidate);
    gl.glProgramUniform1ui(pair_accumulate_program_,
                           pair_accumulate_max_pair_count_location_,
                           pair_scratch_buffers_.pair_capacity);
    gl.glProgramUniform1f(pair_accumulate_program_,
                          pair_accumulate_collision_thickness_location_,
                          collision_thickness_);
    gl.glProgramUniform1ui(pair_accumulate_program_,
                           pair_accumulate_contact_candidate_capacity_location_,
                           pair_scratch_buffers_.contact_candidate_capacity_per_vertex);
    gl.glDispatchCompute(compute_group_count(pair_scratch_buffers_.pair_capacity, pair_accumulate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    gl.glUseProgram(pair_apply_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_cloth_current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_collision_states_binding,
                        collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_contact_normals_binding,
                        collision_view.contact_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_correction_sums_binding,
                        pair_scratch_buffers_.correction_sum);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_correction_counts_binding,
                        pair_scratch_buffers_.correction_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_contact_candidate_counts_binding,
                        pair_scratch_buffers_.contact_candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        pair_apply_contact_candidates_binding,
                        pair_scratch_buffers_.contact_candidate);
    gl.glProgramUniform1ui(pair_apply_program_, pair_apply_vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1ui(pair_apply_program_,
                           pair_apply_max_contacts_per_vertex_location_,
                           collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1ui(pair_apply_program_,
                           pair_apply_contact_candidate_capacity_location_,
                           pair_scratch_buffers_.contact_candidate_capacity_per_vertex);
    gl.glProgramUniform1f(pair_apply_program_, pair_apply_max_correction_length_location_, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, pair_apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    return true;
}

void BodyVertexClothFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    gpu_timer_.release(gl);
#endif

    release_pair_scratch_buffers(gl);

    if (pair_generate_program_ != 0) {
        gl.glDeleteProgram(pair_generate_program_);
    }
    if (pair_accumulate_program_ != 0) {
        gl.glDeleteProgram(pair_accumulate_program_);
    }
    if (pair_apply_program_ != 0) {
        gl.glDeleteProgram(pair_apply_program_);
    }

    pair_generate_program_ = 0;
    pair_accumulate_program_ = 0;
    pair_apply_program_ = 0;
    pair_generate_triangle_count_location_ = -1;
    pair_generate_root_node_index_location_ = -1;
    pair_generate_max_pair_count_location_ = -1;
    pair_accumulate_max_pair_count_location_ = -1;
    pair_accumulate_collision_thickness_location_ = -1;
    pair_accumulate_contact_candidate_capacity_location_ = -1;
    pair_apply_vertex_count_location_ = -1;
    pair_apply_max_contacts_per_vertex_location_ = -1;
    pair_apply_contact_candidate_capacity_location_ = -1;
    pair_apply_max_correction_length_location_ = -1;
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}

bool BodyVertexClothFaceCollisionSolver::ensure_pair_scratch_buffers(std::uint32_t vertex_count,
                                                                     std::uint32_t triangle_count,
                                                                     std::uint32_t max_contacts_per_vertex,
                                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t pair_capacity = std::max(triangle_count, triangle_count * pair_capacity_multiplier);
    const std::uint32_t contact_candidate_capacity_per_vertex = std::max(max_contacts_per_vertex, 1u);
    if (pair_scratch_buffers_.pair_record != 0 &&
        pair_scratch_buffers_.vertex_capacity >= vertex_count &&
        pair_scratch_buffers_.pair_capacity >= pair_capacity &&
        pair_scratch_buffers_.contact_candidate_capacity_per_vertex >= contact_candidate_capacity_per_vertex) {
        return true;
    }

    release_pair_scratch_buffers(gl);

    pair_scratch_buffers_.vertex_capacity = vertex_count;
    pair_scratch_buffers_.pair_capacity = pair_capacity;
    pair_scratch_buffers_.contact_candidate_capacity_per_vertex = contact_candidate_capacity_per_vertex;

    gl.glCreateBuffers(1, &pair_scratch_buffers_.pair_record);
    gl.glCreateBuffers(1, &pair_scratch_buffers_.pair_count);
    gl.glCreateBuffers(1, &pair_scratch_buffers_.correction_sum);
    gl.glCreateBuffers(1, &pair_scratch_buffers_.correction_count);
    gl.glCreateBuffers(1, &pair_scratch_buffers_.contact_candidate_count);
    gl.glCreateBuffers(1, &pair_scratch_buffers_.contact_candidate);

    gl.glNamedBufferData(pair_scratch_buffers_.pair_record,
                         static_cast<GLsizeiptr>(pair_capacity * sizeof(std::uint32_t) * 2u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(pair_scratch_buffers_.pair_count,
                         static_cast<GLsizeiptr>(sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(pair_scratch_buffers_.correction_sum,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::int32_t) * 4u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(pair_scratch_buffers_.correction_count,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(pair_scratch_buffers_.contact_candidate_count,
                         static_cast<GLsizeiptr>(vertex_count * sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(pair_scratch_buffers_.contact_candidate,
                         static_cast<GLsizeiptr>(vertex_count *
                                                 contact_candidate_capacity_per_vertex *
                                                 sizeof(float) *
                                                 4u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    return pair_scratch_buffers_.pair_record != 0 &&
           pair_scratch_buffers_.pair_count != 0 &&
           pair_scratch_buffers_.correction_sum != 0 &&
           pair_scratch_buffers_.correction_count != 0 &&
           pair_scratch_buffers_.contact_candidate_count != 0 &&
           pair_scratch_buffers_.contact_candidate != 0;
}

void BodyVertexClothFaceCollisionSolver::clear_pair_scratch_buffers(QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t zero_uint[4] = {};
    const std::int32_t zero_int[4] = {};
    gl.glClearNamedBufferData(pair_scratch_buffers_.pair_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
    gl.glClearNamedBufferData(pair_scratch_buffers_.correction_sum,
                              GL_RGBA32I,
                              GL_RGBA_INTEGER,
                              GL_INT,
                              zero_int);
    gl.glClearNamedBufferData(pair_scratch_buffers_.correction_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
    gl.glClearNamedBufferData(pair_scratch_buffers_.contact_candidate_count,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              zero_uint);
}

void BodyVertexClothFaceCollisionSolver::release_pair_scratch_buffers(QOpenGLFunctions_4_5_Core& gl) const
{
    if (pair_scratch_buffers_.pair_record != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.pair_record);
    }
    if (pair_scratch_buffers_.pair_count != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.pair_count);
    }
    if (pair_scratch_buffers_.correction_sum != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.correction_sum);
    }
    if (pair_scratch_buffers_.correction_count != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.correction_count);
    }
    if (pair_scratch_buffers_.contact_candidate_count != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.contact_candidate_count);
    }
    if (pair_scratch_buffers_.contact_candidate != 0) {
        gl.glDeleteBuffers(1, &pair_scratch_buffers_.contact_candidate);
    }
    pair_scratch_buffers_ = {};
}

bool BodyVertexClothFaceCollisionSolver::has_pair_programs() const
{
    return pair_generate_program_ != 0 &&
           pair_accumulate_program_ != 0 &&
           pair_apply_program_ != 0;
}
