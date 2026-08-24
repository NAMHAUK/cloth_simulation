#include "simulation/collision/ClothBodyCollisionSolver.h"

#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128;

namespace vf_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint body_triangle_positions = 2;
constexpr GLuint body_triangle_normals = 3;
constexpr GLuint body_previous = 4;
constexpr GLuint body_triangles = 5;
constexpr GLuint candidates = 6;
constexpr GLuint candidate_count = 7;
constexpr GLuint normal_correction_sums = 8;
constexpr GLuint friction_correction_sums = 9;
constexpr GLuint collision_pushouts = 10;
constexpr GLuint cloth_cloth_pushouts = 11;
constexpr GLuint contact_motion_deltas = 12;
constexpr GLuint contact_motion_delta_sums = 13;
}

namespace ee_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_edges = 2;
constexpr GLuint body_previous = 3;
constexpr GLuint body_current = 4;
constexpr GLuint body_edges = 5;
constexpr GLuint candidates = 6;
constexpr GLuint candidate_count = 7;
constexpr GLuint normal_correction_sums = 8;
constexpr GLuint friction_correction_sums = 9;
constexpr GLuint collision_pushouts = 10;
constexpr GLuint cloth_cloth_pushouts = 11;
constexpr GLuint contact_motion_deltas = 12;
constexpr GLuint contact_motion_delta_sums = 13;
}

namespace bf_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_previous = 3;
constexpr GLuint body_current = 4;
constexpr GLuint body_normals = 5;
constexpr GLuint candidates = 6;
constexpr GLuint candidate_count = 7;
constexpr GLuint normal_correction_sums = 8;
constexpr GLuint friction_correction_sums = 9;
constexpr GLuint collision_pushouts = 10;
constexpr GLuint cloth_cloth_pushouts = 11;
constexpr GLuint contact_motion_deltas = 12;
constexpr GLuint contact_motion_delta_sums = 13;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
constexpr GLuint friction_correction_sums = 3;
constexpr GLuint contact_motion_delta_sums = 4;
constexpr GLuint contact_motion_deltas = 5;
}
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
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";
    vf_accumulate_.program =
        load_compute_program(collision_shader_dir / "cloth_vertex_body_face_accumulate.comp",
                             "Cloth vertex/body face candidate accumulation",
                             gl);
    ee_accumulate_.program =
        load_compute_program(collision_shader_dir / "cloth_edge_body_edge_accumulate.comp",
                             "Cloth edge/body edge candidate accumulation",
                             gl);
    bf_accumulate_.program =
        load_compute_program(collision_shader_dir / "body_vertex_cloth_face_accumulate.comp",
                             "Body vertex/cloth face candidate accumulation",
                             gl);
    apply_.program = load_compute_program(collision_shader_dir / "cloth_body_collision_apply.comp",
                                          "Cloth-body collision combined apply",
                                          gl);
    vf_accumulate_.max_candidates = gl.glGetUniformLocation(vf_accumulate_.program, "uMaxCandidateCount");
    vf_accumulate_.thickness = gl.glGetUniformLocation(vf_accumulate_.program, "uCollisionThickness");
    ee_accumulate_.max_candidates = gl.glGetUniformLocation(ee_accumulate_.program, "uMaxCandidateCount");
    ee_accumulate_.thickness = gl.glGetUniformLocation(ee_accumulate_.program, "uCollisionThickness");
    bf_accumulate_.max_candidates = gl.glGetUniformLocation(bf_accumulate_.program, "uMaxCandidateCount");
    bf_accumulate_.thickness = gl.glGetUniformLocation(bf_accumulate_.program, "uCollisionThickness");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");
    apply_.static_friction = gl.glGetUniformLocation(apply_.program, "uStaticFriction");
    apply_.dynamic_friction = gl.glGetUniformLocation(apply_.program, "uDynamicFriction");

    if (vf_accumulate_.max_candidates < 0 ||
        vf_accumulate_.thickness < 0 ||
        ee_accumulate_.max_candidates < 0 ||
        ee_accumulate_.thickness < 0 ||
        bf_accumulate_.max_candidates < 0 ||
        bf_accumulate_.thickness < 0 ||
        apply_.vertex_count < 0 ||
        apply_.max_correction < 0 ||
        apply_.static_friction < 0 ||
        apply_.dynamic_friction < 0) {
        throw std::runtime_error("Cloth-body collision compute shader missing required uniforms.");
    }
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::body_triangle_positions,
                        views.body_triangles.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::body_triangle_normals,
                        views.body_triangles.normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::body_previous,
                        views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::body_triangles,
                        views.body_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::candidates,
                        collision_candidates.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::candidate_count,
                        collision_candidates.count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::normal_correction_sums,
                        views.collision.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::friction_correction_sums,
                        views.collision.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::collision_pushouts,
                        views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::cloth_cloth_pushouts,
                        views.cloth_collision_pushout.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::contact_motion_deltas,
                        views.cloth_contact_motion.contact_motion_delta_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        vf_binding::contact_motion_delta_sums,
                        views.collision.contact_motion_delta_sum_buffer);
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::cloth_edges,
                        views.stretch_constraints.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::body_previous,
                        views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::body_current,
                        views.body_vertices.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::body_edges,
                        views.body_topology.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::candidates,
                        collision_candidates.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::candidate_count,
                        collision_candidates.count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::normal_correction_sums,
                        views.collision.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::friction_correction_sums,
                        views.collision.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::collision_pushouts,
                        views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::cloth_cloth_pushouts,
                        views.cloth_collision_pushout.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::contact_motion_deltas,
                        views.cloth_contact_motion.contact_motion_delta_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        ee_binding::contact_motion_delta_sums,
                        views.collision.contact_motion_delta_sum_buffer);
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::cloth_triangles,
                        views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::body_previous,
                        views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::body_current,
                        views.body_vertices.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::body_normals,
                        views.body_vertices.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::candidates,
                        collision_candidates.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::candidate_count,
                        collision_candidates.count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::normal_correction_sums,
                        views.collision.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::friction_correction_sums,
                        views.collision.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::collision_pushouts,
                        views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::cloth_cloth_pushouts,
                        views.cloth_collision_pushout.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::contact_motion_deltas,
                        views.cloth_contact_motion.contact_motion_delta_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        bf_binding::contact_motion_delta_sums,
                        views.collision.contact_motion_delta_sum_buffer);
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::collision_pushouts,
                        views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::normal_correction_sums,
                        views.collision.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::friction_correction_sums,
                        views.collision.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::contact_motion_delta_sums,
                        views.collision.contact_motion_delta_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::contact_motion_deltas,
                        views.cloth_contact_motion.contact_motion_delta_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glProgramUniform1f(apply_.program, apply_.static_friction, static_friction_);
    gl.glProgramUniform1f(apply_.program, apply_.dynamic_friction, dynamic_friction_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
