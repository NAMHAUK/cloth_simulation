#include "simulation/collision/ClothBodyCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr std::uint32_t apply_local_size = 128;

namespace vf_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint body_triangle_geometry = 2;
constexpr GLuint candidates = 3;
constexpr GLuint candidate_count = 4;
constexpr GLuint normal_correction_sums = 5;
constexpr GLuint friction_correction_sums = 6;
constexpr GLuint body_previous = 7;
constexpr GLuint body_triangles = 8;
constexpr GLuint collision_pushouts = 9;
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
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
constexpr GLuint friction_correction_sums = 3;
}
}

bool ClothBodyCollisionSolver::is_initialized() const
{
    return vf_accumulate_.program != 0 &&
           ee_accumulate_.program != 0 &&
           bf_accumulate_.program != 0 &&
           apply_.program != 0;
}

bool ClothBodyCollisionSolver::initialize(const std::filesystem::path& cloth_vertex_body_face_accumulate_shader_path,
                                          const std::filesystem::path& cloth_edge_body_edge_accumulate_shader_path,
                                          const std::filesystem::path& body_vertex_cloth_face_accumulate_shader_path,
                                          const std::filesystem::path& apply_shader_path,
                                          float collision_thickness,
                                          float max_correction_length,
                                          float static_friction,
                                          float dynamic_friction,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    vf_accumulate_.program = load_compute_program(cloth_vertex_body_face_accumulate_shader_path, "Cloth vertex/body face candidate accumulation", gl);
    ee_accumulate_.program = load_compute_program(cloth_edge_body_edge_accumulate_shader_path, "Cloth edge/body edge candidate accumulation", gl);
    bf_accumulate_.program = load_compute_program(body_vertex_cloth_face_accumulate_shader_path, "Body vertex/cloth face candidate accumulation", gl);
    apply_.program = load_compute_program(apply_shader_path, "Cloth-body collision combined apply", gl);
    if (!is_initialized()) {
        release(gl);
        return false;
    }

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
        std::cerr << "Cloth-body collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
    static_friction_ = static_friction;
    dynamic_friction_ = dynamic_friction;
    return true;
}

bool ClothBodyCollisionSolver::can_solve(const SimulationGpuViews& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_collision_pushout_view(views.cloth_collision_pushout) &&
           views.cloth_motion.vertex_count == views.cloth_collision_pushout.vertex_count &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           is_valid_character_mesh_topology_resource(views.body_topology) &&
           is_valid_character_vertex_buffer_view(views.body_vertices) &&
           is_valid_triangle_geometry_resource(views.body_triangle_geometry) &&
           views.body_topology.triangle_count == views.body_triangle_geometry.triangle_count &&
           views.stretch_constraints.edge_index_buffer != 0 &&
           views.stretch_constraints.constraint_count != 0 &&
           is_valid_edge_bvh_resource(views.body_edge_bvh) &&
           is_valid_collision_candidate_buffer_view(views.collision_candidates) &&
           views.collision_candidates.vertex_capacity >= views.cloth_motion.vertex_count &&
           views.collision_candidates.cloth_vertex_body_face.capacity >= views.cloth_motion.vertex_count * CollisionCandidateBuffers::candidate_capacity_multiplier &&
           views.collision_candidates.cloth_edge_body_edge.capacity >= views.stretch_constraints.constraint_count * CollisionCandidateBuffers::candidate_capacity_multiplier &&
           views.collision_candidates.cloth_face_body_vertex.capacity >= views.cloth_topology.triangle_count * CollisionCandidateBuffers::candidate_capacity_multiplier &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f &&
           dynamic_friction_ >= 0.0f &&
           static_friction_ >= dynamic_friction_;
}

void ClothBodyCollisionSolver::solve(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
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
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
    static_friction_ = 0.0f;
    dynamic_friction_ = 0.0f;
}

void ClothBodyCollisionSolver::clear_correction_sums(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    views.collision_candidates.clear_correction_sums(gl);
}

void ClothBodyCollisionSolver::vf_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_vertex_body_face;
    gl.glUseProgram(vf_accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::body_triangle_geometry, views.body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::candidates, collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::candidate_count, collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::normal_correction_sums, views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::friction_correction_sums, views.collision_candidates.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::body_previous, views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::body_triangles, views.body_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vf_binding::collision_pushouts, views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glProgramUniform1ui(vf_accumulate_.program, vf_accumulate_.max_candidates, collision_candidates.capacity);
    gl.glProgramUniform1f(vf_accumulate_.program, vf_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::ee_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_edge_body_edge;
    gl.glUseProgram(ee_accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::cloth_edges, views.stretch_constraints.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::body_previous, views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::body_current, views.body_vertices.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::body_edges, views.body_edge_bvh.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::candidates, collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::candidate_count, collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::normal_correction_sums, views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::friction_correction_sums, views.collision_candidates.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ee_binding::collision_pushouts, views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glProgramUniform1ui(ee_accumulate_.program, ee_accumulate_.max_candidates, collision_candidates.capacity);
    gl.glProgramUniform1f(ee_accumulate_.program, ee_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::bf_accumulate(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_face_body_vertex;
    gl.glUseProgram(bf_accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::cloth_triangles, views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::body_previous, views.body_vertices.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::body_current, views.body_vertices.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::body_normals, views.body_vertices.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::candidates, collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::candidate_count, collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::normal_correction_sums, views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::friction_correction_sums, views.collision_candidates.friction_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bf_binding::collision_pushouts, views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glProgramUniform1ui(bf_accumulate_.program, bf_accumulate_.max_candidates, collision_candidates.capacity);
    gl.glProgramUniform1f(bf_accumulate_.program, bf_accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionSolver::apply_combined_corrections(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::collision_pushouts, views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::normal_correction_sums, views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::friction_correction_sums, views.collision_candidates.friction_correction_sum_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glProgramUniform1f(apply_.program, apply_.static_friction, static_friction_);
    gl.glProgramUniform1f(apply_.program, apply_.dynamic_friction, dynamic_friction_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
