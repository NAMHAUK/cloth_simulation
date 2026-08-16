#include "simulation/collision/ClothClothCollisionSolver.h"

#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
constexpr std::uint32_t body_triangle_index_build_local_size = 128u;

namespace accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint candidates = 3;
constexpr GLuint candidate_count = 4;
constexpr GLuint normal_correction_sums = 5;
constexpr GLuint body_triangle_geometry = 6;
constexpr GLuint cloth_body_triangle_indices = 7;
}

namespace initial_accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_triangles = 1;
constexpr GLuint candidates = 2;
constexpr GLuint candidate_count = 3;
constexpr GLuint normal_correction_sums = 4;
constexpr GLuint body_triangle_geometry = 5;
constexpr GLuint body_triangle_bvh_nodes = 6;
}

namespace body_triangle_index_build_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_body_triangle_indices = 1;
constexpr GLuint body_triangle_geometry = 2;
constexpr GLuint body_triangle_bvh_nodes = 3;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_cloth_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
}

template <typename... Locations>
bool are_uniform_locations_valid(Locations... locations)
{
    return ((locations >= 0) && ...);
}

bool has_valid_common_solve_views(const SimulationGpuView& views)
{
    const auto garment_count = static_cast<std::size_t>(
        std::count_if(views.garment_vertex_ranges.begin(),
                      views.garment_vertex_ranges.end(),
                      [](const ElementRange& vertex_range) { return vertex_range.count != 0u; }));
    return is_valid_motion_view(views.cloth_motion) &&
           is_valid_collision_pushout_view(views.cloth_collision_pushout) &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           views.cloth_motion.vertex_count == views.cloth_collision_pushout.vertex_count &&
           views.cloth_motion.vertex_count == views.cloth_topology.vertex_count &&
           is_valid_cloth_bvh_buffer_view(views.cloth_bvh) &&
           garment_count == views.cloth_bvh.garment_count &&
           (views.cloth_bvh.garment_count < 2u ||
            (is_valid_triangle_geometry_resource(views.body_triangle_geometry) &&
             is_valid_cloth_cloth_candidate_buffer_view(views.collision_candidates) &&
             views.collision_candidates.vertex_capacity >= views.cloth_motion.vertex_count));
}
}

ClothClothCollisionSolver::ClothClothCollisionSolver(const ClothCollisionParams& params)
    : collision_thickness_(params.thickness),
      collision_stiffness_(params.stiffness),
      max_correction_length_(params.max_correction_length),
      surface_search_radius_(params.body_search_radius)
{}

bool ClothClothCollisionSolver::is_initialized() const
{
    return accumulate_.program != 0 &&
           initial_accumulate_.program != 0 &&
           body_triangle_index_build_.program != 0 &&
           apply_.program != 0;
}

void ClothClothCollisionSolver::initialize(const std::filesystem::path& shader_dir,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";
    accumulate_.program =
        load_compute_program(collision_shader_dir / "cloth_cloth_vertex_face_accumulate.comp",
                             "Cloth-cloth vertex-face candidate accumulation",
                             gl);
    initial_accumulate_.program =
        load_compute_program(collision_shader_dir / "cloth_cloth_initial_layer_accumulate.comp",
                             "Initial cloth-cloth layer candidate accumulation",
                             gl);
    body_triangle_index_build_.program =
        load_compute_program(shader_dir / "cloth" / "setup" / "cloth_body_triangle_index_build.comp",
                             "Cloth body triangle index build",
                             gl);
    apply_.program = load_compute_program(collision_shader_dir / "cloth_cloth_collision_apply.comp",
                                          "Cloth-cloth collision apply",
                                          gl);
    accumulate_.max_candidates = gl.glGetUniformLocation(accumulate_.program, "uMaxCandidateCount");
    accumulate_.collision_thickness = gl.glGetUniformLocation(accumulate_.program, "uCollisionThickness");
    accumulate_.collision_stiffness = gl.glGetUniformLocation(accumulate_.program, "uCollisionStiffness");
    accumulate_.body_triangle_count = gl.glGetUniformLocation(accumulate_.program, "uBodyTriangleCount");
    accumulate_.upper_vertex_offset = gl.glGetUniformLocation(accumulate_.program, "uUpperVertexOffset");
    initial_accumulate_.max_candidates =
        gl.glGetUniformLocation(initial_accumulate_.program, "uMaxCandidateCount");
    initial_accumulate_.collision_thickness =
        gl.glGetUniformLocation(initial_accumulate_.program, "uCollisionThickness");
    initial_accumulate_.collision_stiffness =
        gl.glGetUniformLocation(initial_accumulate_.program, "uCollisionStiffness");
    initial_accumulate_.search_radius_squared =
        gl.glGetUniformLocation(initial_accumulate_.program, "uSearchRadiusSquared");
    initial_accumulate_.upper_vertex_offset =
        gl.glGetUniformLocation(initial_accumulate_.program, "uUpperVertexOffset");
    body_triangle_index_build_.vertex_count =
        gl.glGetUniformLocation(body_triangle_index_build_.program, "uVertexCount");
    body_triangle_index_build_.search_radius_squared =
        gl.glGetUniformLocation(body_triangle_index_build_.program, "uSearchRadiusSquared");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");

    if (!are_uniform_locations_valid(accumulate_.max_candidates,
                                     accumulate_.collision_thickness,
                                     accumulate_.collision_stiffness,
                                     accumulate_.body_triangle_count,
                                     accumulate_.upper_vertex_offset,
                                     initial_accumulate_.max_candidates,
                                     initial_accumulate_.collision_thickness,
                                     initial_accumulate_.collision_stiffness,
                                     initial_accumulate_.search_radius_squared,
                                     initial_accumulate_.upper_vertex_offset,
                                     body_triangle_index_build_.vertex_count,
                                     body_triangle_index_build_.search_radius_squared,
                                     apply_.vertex_count,
                                     apply_.max_correction)) {
        throw std::runtime_error("Cloth-cloth collision compute shader missing required uniforms.");
    }
}

bool ClothClothCollisionSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           has_valid_common_solve_views(views) &&
           (views.cloth_bvh.garment_count < 2u ||
            (is_valid_body_triangle_index_view(views.cloth_body_triangle_indices) &&
             views.cloth_body_triangle_indices.vertex_count == views.cloth_motion.vertex_count));
}

bool ClothClothCollisionSolver::can_solve_initial(const SimulationGpuView& views) const
{
    return is_initialized() &&
           has_valid_common_solve_views(views) &&
           (views.cloth_bvh.garment_count < 2u || is_valid_triangle_bvh_resource(views.body_triangle_bvh));
}

bool ClothClothCollisionSolver::can_update_body_surface_mapping(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_cloth_bvh_buffer_view(views.cloth_bvh) &&
           (views.cloth_bvh.garment_count < 2u ||
            (is_valid_motion_view(views.cloth_motion) &&
             is_valid_body_triangle_index_view(views.cloth_body_triangle_indices) &&
             views.cloth_body_triangle_indices.vertex_count == views.cloth_motion.vertex_count &&
             is_valid_triangle_geometry_resource(views.body_triangle_geometry) &&
             is_valid_triangle_bvh_resource(views.body_triangle_bvh)));
}

void ClothClothCollisionSolver::update_body_surface_mapping(const SimulationGpuView& views,
                                                            QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_update_body_surface_mapping(views));
    if (views.cloth_bvh.garment_count < 2u) {
        return;
    }

    gl.glUseProgram(body_triangle_index_build_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_index_build_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_index_build_binding::cloth_body_triangle_indices,
                        views.cloth_body_triangle_indices.body_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_index_build_binding::body_triangle_geometry,
                        views.body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_index_build_binding::body_triangle_bvh_nodes,
                        views.body_triangle_bvh.node_buffer);
    gl.glProgramUniform1ui(body_triangle_index_build_.program,
                           body_triangle_index_build_.vertex_count,
                           views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(body_triangle_index_build_.program,
                          body_triangle_index_build_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    gl.glDispatchCompute(
        compute_group_count(views.cloth_motion.vertex_count, body_triangle_index_build_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));
    if (views.cloth_bvh.garment_count < 2u) {
        return;
    }

    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset = views.garment_vertex_ranges[GarmentLayer::Upper].offset;
    views.collision_candidates.clear_normal_correction_sums(gl);

    gl.glUseProgram(accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::cloth_triangles,
                        views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::normal_correction_sums,
                        views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::body_triangle_geometry,
                        views.body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        accumulate_binding::cloth_body_triangle_indices,
                        views.cloth_body_triangle_indices.body_triangle_index_buffer);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_candidates, collision_candidates.capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_thickness, collision_thickness_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_stiffness, collision_stiffness_);
    gl.glProgramUniform1ui(accumulate_.program,
                           accumulate_.body_triangle_count,
                           views.body_triangle_geometry.triangle_count);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.upper_vertex_offset, upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::solve_initial(const SimulationGpuView& views,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve_initial(views));
    if (views.cloth_bvh.garment_count < 2u) {
        return;
    }

    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset = views.garment_vertex_ranges[GarmentLayer::Upper].offset;
    views.collision_candidates.clear_normal_correction_sums(gl);

    gl.glUseProgram(initial_accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::cloth_triangles,
                        views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::normal_correction_sums,
                        views.collision_candidates.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::body_triangle_geometry,
                        views.body_triangle_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        initial_accumulate_binding::body_triangle_bvh_nodes,
                        views.body_triangle_bvh.node_buffer);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.max_candidates,
                           collision_candidates.capacity);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.collision_thickness,
                          collision_thickness_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.collision_stiffness,
                          collision_stiffness_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.upper_vertex_offset,
                           upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::apply_corrections(const SimulationGpuView& views,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::cloth_cloth_pushouts,
                        views.cloth_collision_pushout.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        apply_binding::normal_correction_sums,
                        views.collision_candidates.normal_correction_sum_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_.program);
    gl.glDeleteProgram(body_triangle_index_build_.program);
    gl.glDeleteProgram(initial_accumulate_.program);
    gl.glDeleteProgram(accumulate_.program);
    accumulate_ = {};
    initial_accumulate_ = {};
    body_triangle_index_build_ = {};
    apply_ = {};
}
