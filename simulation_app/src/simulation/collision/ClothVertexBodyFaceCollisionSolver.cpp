#include "simulation/collision/ClothVertexBodyFaceCollisionSolver.h"

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
constexpr GLuint body_triangle_geometry = 2;
constexpr GLuint pair_records = 3;
constexpr GLuint pair_count = 4;
constexpr GLuint correction_sums = 5;
}

namespace apply_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint collision_states = 1;
constexpr GLuint correction_sums = 2;
}
}

bool ClothVertexBodyFaceCollisionSolver::is_initialized() const
{
    return has_pair_programs();
}

bool ClothVertexBodyFaceCollisionSolver::initialize(const std::filesystem::path& pair_accumulate_shader_path,
                                                    const std::filesystem::path& pair_apply_shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    accumulate_.program = load_compute_program(pair_accumulate_shader_path, "Cloth vertex/body face pair accumulation", gl);
    apply_.program = load_compute_program(pair_apply_shader_path, "Cloth vertex/body face pair apply", gl);
    if (!has_pair_programs()) {
        release(gl);
        return false;
    }

    accumulate_.max_pairs = gl.glGetUniformLocation(accumulate_.program, "uMaxPairCount");
    accumulate_.thickness = gl.glGetUniformLocation(accumulate_.program, "uCollisionThickness");
    apply_.vertex_count = gl.glGetUniformLocation(apply_.program, "uVertexCount");
    apply_.max_correction = gl.glGetUniformLocation(apply_.program, "uMaxCorrectionLength");

    if (accumulate_.max_pairs < 0 ||
        accumulate_.thickness < 0 ||
        apply_.vertex_count < 0 ||
        apply_.max_correction < 0) {
        std::cerr << "Cloth vertex/body face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    accumulate_timer_.initialize("cloth vertex - body face pair accumulation", gpu_timing_log_interval, gl);
    apply_timer_.initialize("cloth vertex - body face pair apply", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool ClothVertexBodyFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionStateBufferView& collision_view,
                                                   const TriangleGeometryResources& character_geometry,
                                                   const CollisionContactBufferView& collision_contact_view) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_collision_contact_buffer_view(collision_contact_view) &&
           collision_contact_view.vertex_capacity >= motion_view.vertex_count &&
           collision_contact_view.cloth_vertex_body_face.capacity >= motion_view.vertex_count * CollisionContactBuffers::pair_capacity_multiplier &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void ClothVertexBodyFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const TriangleGeometryResources& character_geometry,
                                               const CollisionContactBufferView& collision_contact_view,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view, collision_view, character_geometry, collision_contact_view));

    collision_contact_view.clear_correction_sums(gl);
    run_pair_accumulation_stage(motion_view, character_geometry, collision_contact_view, gl);
    run_pair_apply_stage(motion_view, collision_view, collision_contact_view, gl);
}

void ClothVertexBodyFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(accumulate_.program);
    gl.glDeleteProgram(apply_.program);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    accumulate_timer_.release(gl);
    apply_timer_.release(gl);
#endif

    accumulate_ = {};
    apply_ = {};
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}

void ClothVertexBodyFaceCollisionSolver::run_pair_accumulation_stage(const ClothMotionBufferView& motion_view,
                                                                     const TriangleGeometryResources& character_geometry,
                                                                     const CollisionContactBufferView& collision_contact_view,
                                                                     QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_GPU_TIMING
    const bool gpu_timing_started = accumulate_timer_.begin(gl);
#endif
    const ContactPairBuffers& contact_pairs = collision_contact_view.cloth_vertex_body_face;
    gl.glUseProgram(accumulate_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::body_triangle_geometry, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_records, contact_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::pair_count, contact_pairs.count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, accumulate_binding::correction_sums, collision_contact_view.correction_sum_buffer);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_pairs, contact_pairs.capacity);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.thickness, collision_thickness_);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, contact_pairs.dispatch_command);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        accumulate_timer_.end(gl);
    }
#endif
}

void ClothVertexBodyFaceCollisionSolver::run_pair_apply_stage(const ClothMotionBufferView& motion_view,
                                                              const ClothCollisionStateBufferView& collision_view,
                                                              const CollisionContactBufferView& collision_contact_view,
                                                              QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_GPU_TIMING
    const bool gpu_timing_started = apply_timer_.begin(gl);
#endif
    gl.glUseProgram(apply_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::collision_states, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, apply_binding::correction_sums, collision_contact_view.correction_sum_buffer);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, motion_view.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, pair_apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        apply_timer_.end(gl);
    }
#endif
}

bool ClothVertexBodyFaceCollisionSolver::has_pair_programs() const
{
    return accumulate_.program != 0 &&
           apply_.program != 0;
}
