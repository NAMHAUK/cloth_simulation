#include "simulation/pbd/collision/ClothBodyCollisionSolver.h"
#include "gpu/cloth/ClothGpuState.h"
#include "gpu/scene/PbdGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

namespace {
constexpr std::uint32_t local_size = 128;
}

ClothBodyCollisionSolver::ClothBodyCollisionSolver(const BodyCollisionParams& params)
    : collision_thickness_(params.thickness),
      max_correction_length_(params.max_correction_length),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

void ClothBodyCollisionSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const auto collision_dir = shader_dir / "collision" / "cloth_body";

    {
        auto& shader = cloth_vertex_body_face_;
        shader.program = load_compute_program(collision_dir / "cloth_vertex_body_face_accumulate.comp", gl);

        shader.max_candidates_loc = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
        const GLint thickness_loc = require_uniform_location(shader.program, "uCollisionThickness", gl);
        const GLint static_friction_loc = require_uniform_location(shader.program, "uStaticFriction", gl);
        const GLint dynamic_friction_loc = require_uniform_location(shader.program, "uDynamicFriction", gl);
        gl.glProgramUniform1f(shader.program, thickness_loc, collision_thickness_);
        gl.glProgramUniform1f(shader.program, static_friction_loc, static_friction_);
        gl.glProgramUniform1f(shader.program, dynamic_friction_loc, dynamic_friction_);
    }

    {
        auto& shader = cloth_edge_body_edge_;
        shader.program = load_compute_program(collision_dir / "cloth_edge_body_edge_accumulate.comp", gl);

        shader.max_candidates_loc = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
        const GLint thickness_loc = require_uniform_location(shader.program, "uCollisionThickness", gl);
        const GLint static_friction_loc = require_uniform_location(shader.program, "uStaticFriction", gl);
        const GLint dynamic_friction_loc = require_uniform_location(shader.program, "uDynamicFriction", gl);
        gl.glProgramUniform1f(shader.program, thickness_loc, collision_thickness_);
        gl.glProgramUniform1f(shader.program, static_friction_loc, static_friction_);
        gl.glProgramUniform1f(shader.program, dynamic_friction_loc, dynamic_friction_);
    }

    {
        apply_program_ = load_compute_program(collision_dir / "apply.comp", gl);

        cloth_vertex_count_loc_ = require_uniform_location(apply_program_, "uVertexCount", gl);
        const GLint max_correction_loc = require_uniform_location(apply_program_, "uMaxCorrectionLength", gl);
        gl.glProgramUniform1f(apply_program_, max_correction_loc, max_correction_length_);
    }
}

void ClothBodyCollisionSolver::solve(const ClothGpuState& cloth_state,
                                     const PbdGpuState& pbd_state,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    clear_correction_sums(pbd_state, gl);
    accumulate_cloth_vertex_body_face(pbd_state, gl);
    apply_combined_corrections(cloth_state, gl);

    clear_correction_sums(pbd_state, gl);
    accumulate_cloth_edge_body_edge(pbd_state, gl);
    apply_combined_corrections(cloth_state, gl);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::clear_correction_sums(const PbdGpuState& pbd_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionBuffers& collision = pbd_state.collision_buffers();
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    clear_collision_correction_sum(collision.normal_correction_sum_buffer, gl);
    clear_collision_correction_sum(collision.friction_correction_sum_buffer, gl);
    clear_collision_correction_sum(collision.inward_motion_correction_sum_buffer, gl);
}

void ClothBodyCollisionSolver::accumulate_cloth_vertex_body_face(const PbdGpuState& pbd_state,
                                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& collision_candidates = pbd_state.collision_buffers().cloth_vertex_body_face;
    const auto& shader = cloth_vertex_body_face_;

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates_loc, collision_candidates.max_pairs);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::accumulate_cloth_edge_body_edge(const PbdGpuState& pbd_state,
                                                               QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& collision_candidates = pbd_state.collision_buffers().cloth_edge_body_edge;
    const auto& shader = cloth_edge_body_edge_;

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates_loc, collision_candidates.max_pairs);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::apply_combined_corrections(const ClothGpuState& cloth_state,
                                                          QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t vertex_count = cloth_state.element_counts().vertex;

    gl.glUseProgram(apply_program_);
    gl.glProgramUniform1ui(apply_program_, cloth_vertex_count_loc_, vertex_count);
    gl.glDispatchCompute(compute_group_count(vertex_count, local_size), 1, 1);
}

void ClothBodyCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_program_);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_vertex_body_face_.program);

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    apply_program_ = 0;
    cloth_vertex_count_loc_ = -1;
}
