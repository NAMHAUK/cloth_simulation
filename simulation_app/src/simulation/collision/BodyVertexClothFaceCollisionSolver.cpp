#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr std::uint32_t pair_apply_local_size = 128;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_previous = 3;
constexpr GLuint body_current = 4;
constexpr GLuint body_normals = 5;
constexpr GLuint pair_records = 6;
constexpr GLuint pair_count = 7;
constexpr GLuint normal_correction_sums = 8;
constexpr GLuint friction_correction_sums = 9;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
constexpr GLuint friction_correction_sums = 3;
}
}

bool BodyVertexClothFaceCollisionSolver::is_initialized() const
{
    return has_pair_programs();
}

bool BodyVertexClothFaceCollisionSolver::initialize(const std::filesystem::path& pair_accumulate_shader_path,
                                                    const std::filesystem::path& pair_apply_shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    float static_friction,
                                                    float dynamic_friction,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    accumulate_.program = load_compute_program(pair_accumulate_shader_path, "Body vertex/cloth face pair accumulation", gl);
    apply_.program = load_compute_program(pair_apply_shader_path, "Body vertex/cloth face pair apply", gl);
    if (!has_pair_programs()) {
        release(gl);
        return false;
    }

    accumulate_.max_pairs = gl.glGetUniformLocation(accumulate_.program, "uMaxPairCount");
    accumulate_.thickness = gl.glGetUniformLocation(accumulate_.program, "uCollisionThickness");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");
    apply_.static_friction = gl.glGetUniformLocation(apply_.program, "uStaticFriction");
    apply_.dynamic_friction = gl.glGetUniformLocation(apply_.program, "uDynamicFriction");

    if (accumulate_.max_pairs < 0 ||
        accumulate_.thickness < 0 ||
        apply_.vertex_count < 0 ||
        apply_.max_correction < 0 ||
        apply_.static_friction < 0 ||
        apply_.dynamic_friction < 0) {
        std::cerr << "Body vertex/cloth face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
    static_friction_ = static_friction;
    dynamic_friction_ = dynamic_friction;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    accumulate_timer_.initialize("body vertex - cloth face pair accumulation", gpu_timing_log_interval, gl);
    apply_timer_.initialize("body vertex - cloth face pair apply", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool BodyVertexClothFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionPushoutBufferView& collision_pushout_view,
                                                   const ClothMeshTopologyResources& cloth_topology,
                                                   const CharacterVertexBufferView& character_vertex_view,
                                                   const CollisionPairBufferView& collision_pair_view) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_pushout_view(collision_pushout_view) &&
           motion_view.vertex_count == collision_pushout_view.vertex_count &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_collision_pair_buffer_view(collision_pair_view) &&
           collision_pair_view.vertex_capacity >= motion_view.vertex_count &&
           collision_pair_view.cloth_face_body_vertex.capacity >= cloth_topology.triangle_count * CollisionPairBuffers::pair_capacity_multiplier &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f &&
           dynamic_friction_ >= 0.0f &&
           static_friction_ >= dynamic_friction_;
}

void BodyVertexClothFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionPushoutBufferView& collision_pushout_view,
                                               const ClothMeshTopologyResources& cloth_topology,
                                               const CharacterVertexBufferView& character_vertex_view,
                                               const CollisionPairBufferView& collision_pair_view,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, collision_pushout_view, cloth_topology, character_vertex_view, collision_pair_view));

    collision_pair_view.clear_correction_sums(gl);
    run_pair_accumulation_stage(motion_view, cloth_topology, character_vertex_view, collision_pair_view, gl);
    run_pair_apply_stage(motion_view, collision_pushout_view, collision_pair_view, gl);
}

void BodyVertexClothFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(accumulate_.program);
    gl.glDeleteProgram(apply_.program);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    accumulate_timer_.release(gl);
    apply_timer_.release(gl);
#endif

    accumulate_ = {};
    apply_ = {};
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
    static_friction_ = 0.0f;
    dynamic_friction_ = 0.0f;
}

void BodyVertexClothFaceCollisionSolver::run_pair_accumulation_stage(const ClothMotionBufferView& motion_view,
                                                                     const ClothMeshTopologyResources& cloth_topology,
                                                                     const CharacterVertexBufferView& character_vertex_view,
                                                                     const CollisionPairBufferView& collision_pair_view,
                                                                     QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    const bool gpu_timing_started = accumulate_timer_.begin(gl);
#endif
    const CollisionPairBuffer& collision_pairs = collision_pair_view.cloth_face_body_vertex;
    gl.glUseProgram(accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_triangles, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_previous, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_current, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_normals, character_vertex_view.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_records, collision_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_count, collision_pairs.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::normal_correction_sums, collision_pair_view.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::friction_correction_sums, collision_pair_view.friction_correction_sum_buffer);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_pairs, collision_pairs.capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_pairs.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    if (gpu_timing_started) {
        accumulate_timer_.end(gl);
    }
#endif
}

void BodyVertexClothFaceCollisionSolver::run_pair_apply_stage(const ClothMotionBufferView& motion_view,
                                                              const ClothCollisionPushoutBufferView& collision_pushout_view,
                                                              const CollisionPairBufferView& collision_pair_view,
                                                              QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    const bool gpu_timing_started = apply_timer_.begin(gl);
#endif
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::collision_pushouts, collision_pushout_view.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::normal_correction_sums, collision_pair_view.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::friction_correction_sums, collision_pair_view.friction_correction_sum_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, motion_view.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glProgramUniform1f(apply_.program, apply_.static_friction, static_friction_);
    gl.glProgramUniform1f(apply_.program, apply_.dynamic_friction, dynamic_friction_);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, pair_apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    if (gpu_timing_started) {
        apply_timer_.end(gl);
    }
#endif
}

bool BodyVertexClothFaceCollisionSolver::has_pair_programs() const
{
    return accumulate_.program != 0 &&
           apply_.program != 0;
}
