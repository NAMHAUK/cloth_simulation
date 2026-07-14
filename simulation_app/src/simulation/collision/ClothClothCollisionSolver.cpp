#include "simulation/collision/ClothClothCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace accumulate_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint collision_triangles = 2;
constexpr GLuint pair_records = 3;
constexpr GLuint pair_count = 4;
constexpr GLuint normal_correction_sums = 5;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_pushouts = 1;
constexpr GLuint normal_correction_sums = 2;
}
}

bool ClothClothCollisionSolver::is_initialized() const
{
    return accumulate_.program != 0 && apply_.program != 0;
}

bool ClothClothCollisionSolver::initialize(const std::filesystem::path& accumulate_shader_path,
                                           const std::filesystem::path& apply_shader_path,
                                           float collision_gap,
                                           float barrier_stiffness,
                                           float penetration_tolerance,
                                           float max_correction_length,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    accumulate_.program = load_compute_program(accumulate_shader_path,
                                               "Cloth-cloth vertex-face pair accumulation",
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
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");

    if (accumulate_.max_pairs < 0 ||
        accumulate_.collision_gap < 0 ||
        accumulate_.barrier_stiffness < 0 ||
        accumulate_.penetration_tolerance < 0 ||
        apply_.vertex_count < 0 ||
        apply_.max_correction < 0) {
        std::cerr << "Cloth-cloth collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_gap_ = collision_gap;
    barrier_stiffness_ = barrier_stiffness;
    penetration_tolerance_ = penetration_tolerance;
    max_correction_length_ = max_correction_length;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    accumulate_timer_.initialize("cloth-cloth vertex-face pair accumulation", gpu_timing_log_interval, gl);
    apply_timer_.initialize("cloth-cloth collision apply", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool ClothClothCollisionSolver::can_solve(const SimulationGpuViews& views) const
{
    if (!is_initialized() ||
        !is_valid_motion_view(views.cloth_motion) ||
        !is_valid_collision_pushout_view(views.cloth_collision_pushout) ||
        views.cloth_motion.vertex_count != views.cloth_collision_pushout.vertex_count ||
        !is_valid_cloth_bvh_buffer_view(views.cloth_bvh) ||
        !std::isfinite(collision_gap_) || collision_gap_ <= 0.0f ||
        !std::isfinite(barrier_stiffness_) || barrier_stiffness_ < 0.0f || barrier_stiffness_ > 1.0f ||
        !std::isfinite(penetration_tolerance_) || penetration_tolerance_ < 0.0f ||
        !std::isfinite(max_correction_length_) || max_correction_length_ <= 0.0f) {
        return false;
    }

    if (views.cloth_bvh.garment_layouts->size() < 2u) {
        return true;
    }

    return is_valid_cloth_cloth_pair_buffer_view(views.collision_pairs) &&
           views.collision_pairs.vertex_capacity >= views.cloth_motion.vertex_count;
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
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_pairs, collision_pairs.capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_gap, collision_gap_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.barrier_stiffness, barrier_stiffness_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.penetration_tolerance, penetration_tolerance_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_pairs.dispatch_size);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    if (accumulate_timing_started) {
        accumulate_timer_.end(gl);
    }
#endif

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
    gl.glDeleteProgram(accumulate_.program);
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    apply_timer_.release(gl);
    accumulate_timer_.release(gl);
#endif
    accumulate_ = {};
    apply_ = {};
    collision_gap_ = 0.0f;
    barrier_stiffness_ = 0.0f;
    penetration_tolerance_ = 0.0f;
    max_correction_length_ = 0.0f;
}
