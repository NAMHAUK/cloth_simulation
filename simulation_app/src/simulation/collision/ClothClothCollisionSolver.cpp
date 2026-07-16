#include "simulation/collision/ClothClothCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
constexpr std::uint32_t body_triangle_id_build_local_size = 128u;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint collision_triangles = 2;
constexpr GLuint pair_records = 3;
constexpr GLuint pair_count = 4;
constexpr GLuint normal_correction_sums = 5;
constexpr GLuint character_triangle_geometry = 6;
constexpr GLuint cloth_body_triangle_ids = 7;
}

namespace initial_accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_triangles = 1;
constexpr GLuint pair_records = 2;
constexpr GLuint pair_count = 3;
constexpr GLuint normal_correction_sums = 4;
constexpr GLuint character_triangle_geometry = 5;
constexpr GLuint character_bvh_nodes = 6;
}

namespace body_triangle_id_build_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_body_triangle_ids = 1;
constexpr GLuint character_triangle_geometry = 2;
constexpr GLuint character_bvh_nodes = 3;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
}

template <typename... Locations>
bool are_uniform_locations_valid(Locations... locations)
{
    return ((locations >= 0) && ...);
}

bool has_valid_common_solve_views(const SimulationGpuViews& views)
{
    return is_valid_motion_view(views.cloth_motion) &&
           is_valid_collision_pushout_view(views.cloth_collision_pushout) &&
           views.cloth_motion.vertex_count == views.cloth_collision_pushout.vertex_count &&
           is_valid_cloth_bvh_buffer_view(views.cloth_bvh) &&
           (views.cloth_bvh.garment_layouts->size() < 2u ||
            (is_valid_triangle_geometry_resource(views.character_geometry) &&
             is_valid_cloth_cloth_pair_buffer_view(views.collision_pairs) &&
             views.collision_pairs.vertex_capacity >= views.cloth_motion.vertex_count));
}
}

bool ClothClothCollisionSolver::is_initialized() const
{
    return accumulate_.program != 0 &&
           initial_accumulate_.program != 0 &&
           body_triangle_id_build_.program != 0 &&
           apply_.program != 0;
}

bool ClothClothCollisionSolver::initialize(const std::filesystem::path& accumulate_shader_path,
                                           const std::filesystem::path& initial_accumulate_shader_path,
                                           const std::filesystem::path& body_triangle_id_build_shader_path,
                                           const std::filesystem::path& apply_shader_path,
                                           float collision_gap,
                                           float barrier_stiffness,
                                           float penetration_tolerance,
                                           float max_correction_length,
                                           float surface_search_radius,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    if (!std::isfinite(collision_gap) || collision_gap <= 0.0f ||
        !std::isfinite(barrier_stiffness) || barrier_stiffness < 0.0f || barrier_stiffness > 1.0f ||
        !std::isfinite(penetration_tolerance) || penetration_tolerance < 0.0f ||
        !std::isfinite(max_correction_length) || max_correction_length <= 0.0f ||
        !std::isfinite(surface_search_radius) || surface_search_radius <= 0.0f) {
        std::cerr << "Cloth-cloth collision settings are invalid.\n";
        release(gl);
        return false;
    }

    accumulate_.program = load_compute_program(accumulate_shader_path,
                                               "Cloth-cloth vertex-face pair accumulation",
                                               gl);
    initial_accumulate_.program = load_compute_program(initial_accumulate_shader_path,
                                                       "Initial cloth-cloth layer pair accumulation",
                                                       gl);
    body_triangle_id_build_.program = load_compute_program(body_triangle_id_build_shader_path,
                                                           "Cloth body triangle id build",
                                                           gl);
    apply_.program = load_compute_program(apply_shader_path,
                                          "Cloth-cloth collision apply",
                                          gl);
    if (!is_initialized()) {
        release(gl);
        return false;
    }

    accumulate_.max_pairs = gl.glGetUniformLocation(accumulate_.program, "uMaxPairCount");
    accumulate_.collision_gap = gl.glGetUniformLocation(accumulate_.program, "uCollisionGap");
    accumulate_.barrier_stiffness = gl.glGetUniformLocation(accumulate_.program, "uBarrierStiffness");
    accumulate_.penetration_tolerance = gl.glGetUniformLocation(accumulate_.program, "uPenetrationTolerance");
    accumulate_.character_triangle_count = gl.glGetUniformLocation(accumulate_.program, "uCharacterTriangleCount");
    initial_accumulate_.max_pairs = gl.glGetUniformLocation(initial_accumulate_.program, "uMaxPairCount");
    initial_accumulate_.collision_gap = gl.glGetUniformLocation(initial_accumulate_.program, "uCollisionGap");
    initial_accumulate_.barrier_stiffness = gl.glGetUniformLocation(initial_accumulate_.program, "uBarrierStiffness");
    initial_accumulate_.penetration_tolerance = gl.glGetUniformLocation(initial_accumulate_.program, "uPenetrationTolerance");
    initial_accumulate_.search_radius_squared = gl.glGetUniformLocation(initial_accumulate_.program, "uSearchRadiusSquared");
    body_triangle_id_build_.vertex_count = gl.glGetUniformLocation(body_triangle_id_build_.program, "uVertexCount");
    body_triangle_id_build_.search_radius_squared = gl.glGetUniformLocation(body_triangle_id_build_.program, "uSearchRadiusSquared");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");

    if (!are_uniform_locations_valid(
            accumulate_.max_pairs, accumulate_.collision_gap, accumulate_.barrier_stiffness,
            accumulate_.penetration_tolerance, accumulate_.character_triangle_count,
            initial_accumulate_.max_pairs, initial_accumulate_.collision_gap,
            initial_accumulate_.barrier_stiffness, initial_accumulate_.penetration_tolerance,
            initial_accumulate_.search_radius_squared,
            body_triangle_id_build_.vertex_count, body_triangle_id_build_.search_radius_squared,
            apply_.vertex_count, apply_.max_correction)) {
        std::cerr << "Cloth-cloth collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_gap_ = collision_gap;
    barrier_stiffness_ = barrier_stiffness;
    penetration_tolerance_ = penetration_tolerance;
    max_correction_length_ = max_correction_length;
    surface_search_radius_ = surface_search_radius;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    accumulate_timer_.initialize("cloth-cloth vertex-face pair accumulation", gpu_timing_log_interval, gl);
    apply_timer_.initialize("cloth-cloth collision apply", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool ClothClothCollisionSolver::can_solve(const SimulationGpuViews& views) const
{
    return is_initialized() &&
           has_valid_common_solve_views(views) &&
           (views.cloth_bvh.garment_layouts->size() < 2u ||
            (is_valid_body_triangle_id_view(views.cloth_body_triangle_ids) &&
             views.cloth_body_triangle_ids.vertex_count == views.cloth_motion.vertex_count));
}

bool ClothClothCollisionSolver::can_solve_initial(const SimulationGpuViews& views) const
{
    return is_initialized() &&
           has_valid_common_solve_views(views) &&
           (views.cloth_bvh.garment_layouts->size() < 2u ||
            is_valid_triangle_bvh_resource(views.character_bvh));
}

bool ClothClothCollisionSolver::can_build_body_triangle_ids(const SimulationGpuViews& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_body_triangle_id_view(views.cloth_body_triangle_ids) &&
           views.cloth_body_triangle_ids.vertex_count == views.cloth_motion.vertex_count &&
           is_valid_triangle_geometry_resource(views.character_geometry) &&
           is_valid_triangle_bvh_resource(views.character_bvh);
}

bool ClothClothCollisionSolver::build_body_triangle_ids(const SimulationGpuViews& views,
                                                        QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_build_body_triangle_ids(views)) {
        return false;
    }

    gl.glUseProgram(body_triangle_id_build_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_id_build_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_id_build_binding::cloth_body_triangle_ids,
                        views.cloth_body_triangle_ids.body_triangle_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_id_build_binding::character_triangle_geometry,
                        views.character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        body_triangle_id_build_binding::character_bvh_nodes,
                        views.character_bvh.node_buffer);
    gl.glProgramUniform1ui(body_triangle_id_build_.program,
                           body_triangle_id_build_.vertex_count,
                           views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(body_triangle_id_build_.program,
                          body_triangle_id_build_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count,
                                             body_triangle_id_build_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    return true;
}

void ClothClothCollisionSolver::solve(const SimulationGpuViews& views,
                                      QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));
    if (views.cloth_bvh.garment_layouts->size() < 2u) {
        return;
    }

    const CollisionPairBuffer& collision_pairs = views.collision_pairs.cloth_cloth_vertex_face;
    views.collision_pairs.clear_normal_correction_sums(gl);

#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    const bool accumulate_timing_started = accumulate_timer_.begin(gl);
#endif
    gl.glUseProgram(accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_previous, views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::collision_triangles, views.cloth_bvh.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_records, collision_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_count, collision_pairs.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::normal_correction_sums, views.collision_pairs.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::character_triangle_geometry, views.character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_body_triangle_ids, views.cloth_body_triangle_ids.body_triangle_id_buffer);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_pairs, collision_pairs.capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_gap, collision_gap_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.barrier_stiffness, barrier_stiffness_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.penetration_tolerance, penetration_tolerance_);
    gl.glProgramUniform1ui(accumulate_.program,
                           accumulate_.character_triangle_count,
                           views.character_geometry.triangle_count);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_pairs.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    if (accumulate_timing_started) {
        accumulate_timer_.end(gl);
    }
#endif

    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::solve_initial(const SimulationGpuViews& views,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve_initial(views));
    if (views.cloth_bvh.garment_layouts->size() < 2u) {
        return;
    }

    const CollisionPairBuffer& collision_pairs = views.collision_pairs.cloth_cloth_vertex_face;
    views.collision_pairs.clear_normal_correction_sums(gl);

    gl.glUseProgram(initial_accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::collision_triangles, views.cloth_bvh.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::pair_records, collision_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::pair_count, collision_pairs.pair_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::normal_correction_sums, views.collision_pairs.normal_correction_sum_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::character_triangle_geometry, views.character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, initial_accumulate_binding::character_bvh_nodes, views.character_bvh.node_buffer);
    gl.glProgramUniform1ui(initial_accumulate_.program, initial_accumulate_.max_pairs, collision_pairs.capacity);
    gl.glProgramUniform1f(initial_accumulate_.program, initial_accumulate_.collision_gap, collision_gap_);
    gl.glProgramUniform1f(initial_accumulate_.program, initial_accumulate_.barrier_stiffness, barrier_stiffness_);
    gl.glProgramUniform1f(initial_accumulate_.program, initial_accumulate_.penetration_tolerance, penetration_tolerance_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_pairs.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::apply_corrections(const SimulationGpuViews& views,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    const bool apply_timing_started = apply_timer_.begin(gl);
#endif
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::cloth_current, views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::collision_pushouts, views.cloth_collision_pushout.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::normal_correction_sums, views.collision_pairs.normal_correction_sum_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    if (apply_timing_started) {
        apply_timer_.end(gl);
    }
#endif
}

void ClothClothCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_.program);
    gl.glDeleteProgram(body_triangle_id_build_.program);
    gl.glDeleteProgram(initial_accumulate_.program);
    gl.glDeleteProgram(accumulate_.program);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    apply_timer_.release(gl);
    accumulate_timer_.release(gl);
#endif
    accumulate_ = {};
    initial_accumulate_ = {};
    body_triangle_id_build_ = {};
    apply_ = {};
    collision_gap_ = 0.0f;
    barrier_stiffness_ = 0.0f;
    penetration_tolerance_ = 0.0f;
    max_correction_length_ = 0.0f;
    surface_search_radius_ = 0.0f;
}
