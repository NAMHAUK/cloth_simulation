#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
constexpr std::uint32_t pair_generate_local_size = 128;
constexpr std::uint32_t pair_accumulate_local_size = 128;
constexpr std::uint32_t pair_apply_local_size = 128;
#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
constexpr std::uint32_t gpu_timing_log_interval = 512u;
#endif

namespace generate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_vertex_ids = 3;
constexpr GLuint body_bvh = 4;
constexpr GLuint body_current = 5;
constexpr GLuint body_previous = 6;
constexpr GLuint pair_records = 7;
constexpr GLuint pair_count = 8;
}

namespace accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_previous = 3;
constexpr GLuint body_current = 4;
constexpr GLuint body_normals = 5;
constexpr GLuint pair_records = 6;
constexpr GLuint pair_count = 7;
constexpr GLuint correction_sums = 8;
constexpr GLuint contact_counts = 9;
constexpr GLuint contacts = 10;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_states = 1;
constexpr GLuint contact_normals = 2;
constexpr GLuint correction_sums = 3;
constexpr GLuint contact_counts = 4;
constexpr GLuint contacts = 5;
}

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
    generate_.program = load_compute_program(pair_generate_shader_path, "Body vertex/cloth face pair generation", gl);
    accumulate_.program = load_compute_program(pair_accumulate_shader_path, "Body vertex/cloth face pair accumulation", gl);
    apply_.program = load_compute_program(pair_apply_shader_path, "Body vertex/cloth face pair apply", gl);
    if (!has_pair_programs()) {
        release(gl);
        return false;
    }

    generate_.triangle_count = gl.glGetUniformLocation(generate_.program, "uTriangleCount");
    generate_.max_pairs = gl.glGetUniformLocation(generate_.program, "uMaxPairCount");
    generate_.thickness = gl.glGetUniformLocation(generate_.program, "uCollisionThickness");
    accumulate_.max_pairs = gl.glGetUniformLocation(accumulate_.program, "uMaxPairCount");
    accumulate_.thickness = gl.glGetUniformLocation(accumulate_.program, "uCollisionThickness");
    accumulate_.contact_capacity = gl.glGetUniformLocation(accumulate_.program, "uContactCandidateCapacityPerVertex");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_contacts = gl.glGetUniformLocation(apply_.program, "uMaxContactsPerVertex");
    apply_.contact_capacity = gl.glGetUniformLocation(apply_.program, "uContactCandidateCapacityPerVertex");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");

    if (generate_.triangle_count < 0 ||
        generate_.max_pairs < 0 ||
        generate_.thickness < 0 ||
        accumulate_.max_pairs < 0 ||
        accumulate_.thickness < 0 ||
        accumulate_.contact_capacity < 0 ||
        apply_.vertex_count < 0 ||
        apply_.max_contacts < 0 ||
        apply_.contact_capacity < 0 ||
        apply_.max_correction < 0) {
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
                                                   const VertexBvhResources& body_vertex_bvh,
                                                   const CollisionWorkspaceBufferView& collision_workspace_view) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_vertex_bvh_resource(body_vertex_bvh) &&
           is_valid_collision_workspace_buffer_view(collision_workspace_view) &&
           collision_workspace_view.vertex_capacity >= motion_view.vertex_count &&
           collision_workspace_view.pair_capacity >= cloth_topology.triangle_count * CollisionWorkspaceBuffers::pair_capacity_multiplier &&
           collision_workspace_view.contact_candidate_capacity_per_vertex >= collision_view.max_contacts_per_vertex &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void BodyVertexClothFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const ClothMeshTopologyResources& cloth_topology,
                                               const CharacterVertexBufferView& character_vertex_view,
                                               const VertexBvhResources& body_vertex_bvh,
                                               const CollisionWorkspaceBufferView& collision_workspace_view,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, collision_view, cloth_topology, character_vertex_view, body_vertex_bvh, collision_workspace_view));

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    const bool gpu_timing_started = gpu_timer_.begin(gl);
#endif

    collision_workspace_view.clear(gl);
    run_pair_generation_stage(motion_view, cloth_topology, character_vertex_view, body_vertex_bvh, collision_workspace_view, gl);
    run_pair_accumulation_stage(motion_view, cloth_topology, character_vertex_view, collision_workspace_view, gl);
    run_pair_apply_stage(motion_view, collision_view, collision_workspace_view, gl);

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    if (gpu_timing_started) {
        gpu_timer_.end(gl);
    }
#endif
}

void BodyVertexClothFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    gpu_timer_.release(gl);
#endif

    gl.glDeleteProgram(generate_.program);
    gl.glDeleteProgram(accumulate_.program);
    gl.glDeleteProgram(apply_.program);

    generate_ = {};
    accumulate_ = {};
    apply_ = {};
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}

void BodyVertexClothFaceCollisionSolver::run_pair_generation_stage(const ClothMotionBufferView& motion_view,
                                                                   const ClothMeshTopologyResources& cloth_topology,
                                                                   const CharacterVertexBufferView& character_vertex_view,
                                                                   const VertexBvhResources& body_vertex_bvh,
                                                                   const CollisionWorkspaceBufferView& collision_workspace_view,
                                                                   QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(generate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::cloth_triangles, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::body_vertex_ids, body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::body_bvh, body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::body_current, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::body_previous, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::pair_records, collision_workspace_view.pair_record_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, generate_binding::pair_count, collision_workspace_view.pair_count_buffer);
    gl.glProgramUniform1ui(generate_.program, generate_.triangle_count, cloth_topology.triangle_count);
    gl.glProgramUniform1ui(generate_.program, generate_.max_pairs, collision_workspace_view.pair_capacity);
    gl.glProgramUniform1f(generate_.program, generate_.thickness, collision_thickness_);
    gl.glDispatchCompute(compute_group_count(cloth_topology.triangle_count, pair_generate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void BodyVertexClothFaceCollisionSolver::run_pair_accumulation_stage(const ClothMotionBufferView& motion_view,
                                                                     const ClothMeshTopologyResources& cloth_topology,
                                                                     const CharacterVertexBufferView& character_vertex_view,
                                                                     const CollisionWorkspaceBufferView& collision_workspace_view,
                                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_triangles, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_previous, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_current, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_normals, character_vertex_view.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_records, collision_workspace_view.pair_record_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_count, collision_workspace_view.pair_count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::correction_sums, collision_workspace_view.correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::contact_counts, collision_workspace_view.contact_candidate_count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::contacts, collision_workspace_view.contact_candidate_buffer);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_pairs, collision_workspace_view.pair_capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.thickness, collision_thickness_);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.contact_capacity, collision_workspace_view.contact_candidate_capacity_per_vertex);
    gl.glDispatchCompute(compute_group_count(collision_workspace_view.pair_capacity, pair_accumulate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void BodyVertexClothFaceCollisionSolver::run_pair_apply_stage(const ClothMotionBufferView& motion_view,
                                                              const ClothCollisionStateBufferView& collision_view,
                                                              const CollisionWorkspaceBufferView& collision_workspace_view,
                                                              QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::collision_states, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::contact_normals, collision_view.contact_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::correction_sums, collision_workspace_view.correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::contact_counts, collision_workspace_view.contact_candidate_count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::contacts, collision_workspace_view.contact_candidate_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, motion_view.vertex_count);
    gl.glProgramUniform1ui(apply_.program, apply_.max_contacts, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1ui(apply_.program, apply_.contact_capacity, collision_workspace_view.contact_candidate_capacity_per_vertex);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, pair_apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

bool BodyVertexClothFaceCollisionSolver::has_pair_programs() const
{
    return generate_.program != 0 &&
           accumulate_.program != 0 &&
           apply_.program != 0;
}
