#include "simulation/collision/ClothBodyCollisionSolver.h"

#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128;
}

ClothBodyCollisionSolver::ClothBodyCollisionSolver(const BodyCollisionParams& params)
    : collision_thickness_(params.thickness),
      max_correction_length_(params.max_correction_length),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

bool ClothBodyCollisionSolver::is_initialized() const
{
    return vf_accumulate_.program != 0 &&
           ee_accumulate_.program != 0 &&
           bf_accumulate_.program != 0 &&
           apply_.program != 0;
}

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
    vf_accumulate_.max_candidates =
        require_uniform_location(vf_accumulate_.program, "uMaxCandidateCount", gl);
    vf_accumulate_.thickness = require_uniform_location(vf_accumulate_.program, "uCollisionThickness", gl);
    ee_accumulate_.max_candidates =
        require_uniform_location(ee_accumulate_.program, "uMaxCandidateCount", gl);
    ee_accumulate_.thickness = require_uniform_location(ee_accumulate_.program, "uCollisionThickness", gl);
    bf_accumulate_.max_candidates =
        require_uniform_location(bf_accumulate_.program, "uMaxCandidateCount", gl);
    bf_accumulate_.thickness = require_uniform_location(bf_accumulate_.program, "uCollisionThickness", gl);
    apply_.vertex_count = require_uniform_location(apply_.program, "uVertexCount", gl);
    apply_.max_correction = require_uniform_location(apply_.program, "uMaxCorrectionLength", gl);
    apply_.static_friction = require_uniform_location(apply_.program, "uStaticFriction", gl);
    apply_.dynamic_friction = require_uniform_location(apply_.program, "uDynamicFriction", gl);
}

bool ClothBodyCollisionSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_collision_pushout_view(views.cloth_collision_pushout) &&
           is_valid_contact_motion_view(views.cloth_contact_motion) &&
           views.cloth_motion.vertex_count == views.cloth_collision_pushout.vertex_count &&
           views.cloth_motion.vertex_count == views.cloth_contact_motion.vertex_count &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           is_valid_character_mesh_topology_resource(views.body_topology) &&
           is_valid_character_vertex_buffer_view(views.body_vertices) &&
           is_valid_body_triangle_resource(views.body_triangles) &&
           views.body_topology.triangle_count == views.body_triangles.triangle_count &&
           views.stretch_constraints.edge_index_buffer != 0 &&
           views.stretch_constraints.constraint_count != 0 &&
           is_valid_bvh_buffer_view(views.body_edge_bvh) &&
           is_valid_collision_candidate_buffer_view(views.collision) &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f &&
           dynamic_friction_ >= 0.0f &&
           static_friction_ >= dynamic_friction_;
}

void ClothBodyCollisionSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));

    clear_correction_sums(views, gl);
    vf_accumulate(views, gl);
    apply_combined_corrections(views, gl);

    clear_correction_sums(views, gl);
    ee_accumulate(views, gl);
    apply_combined_corrections(views, gl);

    clear_correction_sums(views, gl);
    bf_accumulate(views, gl);
    apply_combined_corrections(views, gl);
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

void ClothBodyCollisionSolver::clear_correction_sums(const SimulationGpuView& views,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    clear_collision_correction_sum(views.collision.normal_correction_sum_buffer, gl);
    clear_collision_correction_sum(views.collision.friction_correction_sum_buffer, gl);
    clear_collision_correction_sum(views.collision.contact_motion_delta_sum_buffer, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::vf_accumulate(const SimulationGpuView& views,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& collision_candidates = views.collision.cloth_vertex_body_face;
    gl.glUseProgram(vf_accumulate_.program);
    gl.glProgramUniform1ui(vf_accumulate_.program,
                           vf_accumulate_.max_candidates,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1f(vf_accumulate_.program, vf_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::ee_accumulate(const SimulationGpuView& views,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& collision_candidates = views.collision.cloth_edge_body_edge;
    gl.glUseProgram(ee_accumulate_.program);
    gl.glProgramUniform1ui(ee_accumulate_.program,
                           ee_accumulate_.max_candidates,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1f(ee_accumulate_.program, ee_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::bf_accumulate(const SimulationGpuView& views,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& collision_candidates = views.collision.cloth_face_body_vertex;
    gl.glUseProgram(bf_accumulate_.program);
    gl.glProgramUniform1ui(bf_accumulate_.program,
                           bf_accumulate_.max_candidates,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1f(bf_accumulate_.program, bf_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::apply_combined_corrections(const SimulationGpuView& views,
                                                          QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(apply_.program);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glProgramUniform1f(apply_.program, apply_.static_friction, static_friction_);
    gl.glProgramUniform1f(apply_.program, apply_.dynamic_friction, dynamic_friction_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
