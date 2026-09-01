#include "simulation/collision/ClothBodyCollisionSolver.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;
}

ClothBodyCollisionSolver::ClothBodyCollisionSolver(const BodyCollisionParams& params)
    : collision_thickness_(params.thickness),
      max_correction_length_(params.max_correction_length),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

void ClothBodyCollisionSolver::initialize(const std::filesystem::path& shader_dir,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path cloth_body_shader_dir = shader_dir / "collision" / "cloth_body";
    vf_accumulate_.program =
        load_compute_program(cloth_body_shader_dir / "cloth_vertex_body_face_accumulate.comp", gl);
    ee_accumulate_.program =
        load_compute_program(cloth_body_shader_dir / "cloth_edge_body_edge_accumulate.comp", gl);
    bf_accumulate_.program =
        load_compute_program(cloth_body_shader_dir / "body_vertex_cloth_face_accumulate.comp", gl);
    apply_.program = load_compute_program(cloth_body_shader_dir / "apply.comp", gl);
    vf_accumulate_.max_candidates_loc =
        require_uniform_location(vf_accumulate_.program, "uMaxCandidateCount", gl);
    ee_accumulate_.max_candidates_loc =
        require_uniform_location(ee_accumulate_.program, "uMaxCandidateCount", gl);
    bf_accumulate_.max_candidates_loc =
        require_uniform_location(bf_accumulate_.program, "uMaxCandidateCount", gl);
    apply_.vertex_count_loc = require_uniform_location(apply_.program, "uVertexCount", gl);

    const GLint vf_thickness_loc =
        require_uniform_location(vf_accumulate_.program, "uCollisionThickness", gl);
    const GLint ee_thickness_loc =
        require_uniform_location(ee_accumulate_.program, "uCollisionThickness", gl);
    const GLint bf_thickness_loc =
        require_uniform_location(bf_accumulate_.program, "uCollisionThickness", gl);
    const GLint max_correction_loc = require_uniform_location(apply_.program, "uMaxCorrectionLength", gl);
    const GLint static_friction_loc = require_uniform_location(apply_.program, "uStaticFriction", gl);
    const GLint dynamic_friction_loc = require_uniform_location(apply_.program, "uDynamicFriction", gl);

    gl.glProgramUniform1f(vf_accumulate_.program, vf_thickness_loc, collision_thickness_);
    gl.glProgramUniform1f(ee_accumulate_.program, ee_thickness_loc, collision_thickness_);
    gl.glProgramUniform1f(bf_accumulate_.program, bf_thickness_loc, collision_thickness_);
    gl.glProgramUniform1f(apply_.program, max_correction_loc, max_correction_length_);
    gl.glProgramUniform1f(apply_.program, static_friction_loc, static_friction_);
    gl.glProgramUniform1f(apply_.program, dynamic_friction_loc, dynamic_friction_);
}

void ClothBodyCollisionSolver::solve(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    clear_correction_sums(gpu_state, gl);
    vf_accumulate(gpu_state, gl);
    apply_combined_corrections(gpu_state, gl);

    clear_correction_sums(gpu_state, gl);
    ee_accumulate(gpu_state, gl);
    apply_combined_corrections(gpu_state, gl);

    clear_correction_sums(gpu_state, gl);
    bf_accumulate(gpu_state, gl);
    apply_combined_corrections(gpu_state, gl);
}

void ClothBodyCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_.program);
    gl.glDeleteProgram(bf_accumulate_.program);
    gl.glDeleteProgram(ee_accumulate_.program);
    gl.glDeleteProgram(vf_accumulate_.program);

    vf_accumulate_ = {};
    ee_accumulate_ = {};
    bf_accumulate_ = {};
    apply_ = {};
}

void ClothBodyCollisionSolver::clear_correction_sums(const SceneGpuState& gpu_state,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionBuffers& collision = gpu_state.collision_buffers();
    clear_collision_correction_sum(collision.normal_correction_sum_buffer, gl);
    clear_collision_correction_sum(collision.friction_correction_sum_buffer, gl);
    clear_collision_correction_sum(collision.contact_motion_delta_sum_buffer, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::vf_accumulate(const SceneGpuState& gpu_state,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& collision_candidates =
        gpu_state.collision_buffers().cloth_vertex_body_face;
    gl.glUseProgram(vf_accumulate_.program);
    gl.glProgramUniform1ui(vf_accumulate_.program,
                           vf_accumulate_.max_candidates_loc,
                           collision_candidates.max_pairs);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::ee_accumulate(const SceneGpuState& gpu_state,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& collision_candidates = gpu_state.collision_buffers().cloth_edge_body_edge;
    gl.glUseProgram(ee_accumulate_.program);
    gl.glProgramUniform1ui(ee_accumulate_.program,
                           ee_accumulate_.max_candidates_loc,
                           collision_candidates.max_pairs);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::bf_accumulate(const SceneGpuState& gpu_state,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& collision_candidates = gpu_state.collision_buffers().cloth_face_body_vertex;
    gl.glUseProgram(bf_accumulate_.program);
    gl.glProgramUniform1ui(bf_accumulate_.program,
                           bf_accumulate_.max_candidates_loc,
                           collision_candidates.max_pairs);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::apply_combined_corrections(const SceneGpuState& gpu_state,
                                                          QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t vertex_count = gpu_state.cloth_gpu_state().element_counts().vertex;
    gl.glUseProgram(apply_.program);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count_loc, vertex_count);
    gl.glDispatchCompute(compute_group_count(vertex_count, local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
