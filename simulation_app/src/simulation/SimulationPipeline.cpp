#include "simulation/SimulationPipeline.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>

#include <glm/vec3.hpp>

namespace {
// Substep start, bounds pair, detection pair, correction pairs, substep end.
constexpr std::size_t fixed_timestamps_per_substep = 6;

GLuint64 elapsed_nanoseconds(GLuint begin, GLuint end, QOpenGLFunctions_4_5_Core& gl)
{
    GLuint64 start_time = 0;
    GLuint64 end_time = 0;
    gl.glGetQueryObjectui64v(begin, GL_QUERY_RESULT, &start_time);
    gl.glGetQueryObjectui64v(end, GL_QUERY_RESULT, &end_time);
    return end_time - start_time;
}
}

SimulationPipeline::SimulationPipeline(SimulationParams params)
    : params_(params),
      cloth_integrator_(params.integration.gravity,
                        params.integration.velocity_damping,
                        params.integration.reference_frame_inertia_scale,
                        params.integration.reference_frame_max_linear_acceleration,
                        params.integration.reference_frame_max_angular_acceleration),
      stretch_constraint_solver_(params.constraints.stretch_stiffness),
      bending_constraint_solver_(params.constraints.bending_stiffness),
      attachment_constraint_solver_(params.constraints.attachment_stiffness),
      ground_collision_solver_(params.collisions.ground),
      collision_detector_(params.collisions.cloth),
      cloth_body_collision_solver_(params.collisions.body),
      cloth_cloth_collision_solver_(params.collisions.cloth),
      garment_prefit_solver_(params.prefit)
{}

// Initialization
void SimulationPipeline::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_valid_simulation_params(params_)) {
        throw std::runtime_error("Cannot initialize simulation pipeline with invalid parameters.");
    }

    substep_dt_ = params_.step.dt() / static_cast<float>(params_.step.substep_count);

    cloth_integrator_.initialize(shader_dir, substep_dt_, gl);
    stretch_constraint_solver_.initialize(shader_dir, gl);
    bending_constraint_solver_.initialize(shader_dir, gl);
    attachment_constraint_solver_.initialize(shader_dir, gl);
    ground_collision_solver_.initialize(shader_dir, gl);
    collision_detector_.initialize(shader_dir, gl);
    cloth_body_collision_solver_.initialize(shader_dir, gl);
    cloth_cloth_collision_solver_.initialize(shader_dir, gl);
    garment_prefit_solver_.initialize(shader_dir, gl);

    initialized_ = true;
}

// Simulation
void SimulationPipeline::prefit_garments(SceneGpuState& gpu_state,
                                         std::uint32_t iteration_count,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());

    params_.step.iteration_count = iteration_count;
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();

    // Garment pre-fit
    for (std::uint32_t iteration = 0; iteration < params_.prefit.iteration_count; ++iteration) {
        gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
        for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
            if (cloth_state.garment_states()[layer].vertex_count > 0u) {
                garment_prefit_solver_.solve(cloth_state, layer, gl);
            }
        }
        stretch_constraint_solver_.solve(cloth_state, gl);
        bending_constraint_solver_.solve(cloth_state, gl);

        for (std::uint32_t collision_iteration = 0; collision_iteration < params_.step.iteration_count;
             ++collision_iteration) {
            gpu_state.update_cloth_bvh_bounds(params_.collisions.cloth.initial_detection_distance, gl);
            collision_detector_.detect_prefit(gpu_state, gl);
            cloth_cloth_collision_solver_.solve_initial(gpu_state, gl);
        }
    }

    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::step(SceneState& scene,
                              SceneGpuState& gpu_state,
                              std::uint32_t motion_step_index,
                              QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());
    collect_timings(gl);

    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    const auto motion_end_step = static_cast<std::uint64_t>(scene.character_motion().frame_count - 1u) * params_.step.motion_stride();
    TimingBatch* timing = motion_step_index < motion_end_step ? &begin_timing_batch(gl) : nullptr;
    const std::size_t timestamps_per_substep = fixed_timestamps_per_substep + params_.step.iteration_count * 2u;

    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        const GLuint* timestamps = timing ? timing->queries.data() + substep * timestamps_per_substep : nullptr;
        if (timestamps) {
            gl.glQueryCounter(timestamps[0], GL_TIMESTAMP);
        }
        update_character_motion(scene, gpu_state, motion_step_index, substep + 1u, gl);
        integrate_cloth(scene, cloth_state, gl);

        if (timestamps) {
            gl.glQueryCounter(timestamps[1], GL_TIMESTAMP);
        }
        gpu_state.update_cloth_bvh_bounds(params_.collisions.cloth.detection_distance, gl);
        if (timestamps) {
            gl.glQueryCounter(timestamps[2], GL_TIMESTAMP);
        }
        collision_detector_.detect(gpu_state, gl, timestamps ? timestamps + 3 : nullptr);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            stretch_constraint_solver_.solve(cloth_state, gl);
            bending_constraint_solver_.solve(cloth_state, gl);
            attachment_constraint_solver_.solve(cloth_state, gl);
            cloth_body_collision_solver_.solve(gpu_state, gl);
            cloth_cloth_collision_solver_.solve(gpu_state,
                                                gl,
                                                timestamps ? timestamps + 5 + iteration * 2u : nullptr);
            ground_collision_solver_.solve(cloth_state, gl);
        }
        if (timestamps) {
            gl.glQueryCounter(timestamps[timestamps_per_substep - 1u], GL_TIMESTAMP);
        }
    }
    if (timing && static_cast<std::uint64_t>(motion_step_index) + 1u == motion_end_step) {
        should_print_timings_ = true;
    }

    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::update_character_motion(SceneState& scene,
                                                 SceneGpuState& gpu_state,
                                                 std::uint32_t motion_step_index,
                                                 std::uint32_t substep,
                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    const float motion_frame_position = params_.step.motion_frame_position(motion_step_index, substep);
    const float frame_alpha = scene.motion_frame_alpha(motion_frame_position);
    scene.update_reference_frame_kinematics(frame_alpha, substep_dt_);
    gpu_state.update_character_pose(scene, frame_alpha, gl);
}

void SimulationPipeline::integrate_cloth(const SceneState& scene,
                                         const ClothGpuState& cloth_state,
                                         QOpenGLFunctions_4_5_Core& gl) const
{
    for (const GarmentObject& garment : scene.garments()) {
        const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];
        assert(garment_state.vertex_count != 0u);

        const auto& kinematics = scene.reference_frame_kinematics(garment.mesh.garment_category);
        cloth_integrator_.integrate(cloth_state, garment.layer, kinematics, gl);
    }
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// GPU timing
void SimulationPipeline::collect_timings(QOpenGLFunctions_4_5_Core& gl)
{
    bool has_pending_results = false;
    for (auto& batch : timing_batches_) {
        if (!batch.is_pending) {
            continue;
        }
        GLint is_available = GL_FALSE;
        gl.glGetQueryObjectiv(batch.queries.back(), GL_QUERY_RESULT_AVAILABLE, &is_available);
        if (is_available == GL_FALSE) {
            has_pending_results |= batch.motion_generation == motion_generation_;
            continue;
        }
        batch.is_pending = false;
        if (batch.motion_generation != motion_generation_) {
            continue;
        }

        const std::size_t stride = fixed_timestamps_per_substep + batch.iteration_count * 2u;
        for (std::uint32_t substep = 0; substep < batch.substep_count; ++substep) {
            const GLuint* queries = batch.queries.data() + substep * stride;
            timing_totals_.substep_ns += elapsed_nanoseconds(queries[0], queries[stride - 1u], gl);
            timing_totals_.cloth_bounds_ns += elapsed_nanoseconds(queries[1], queries[2], gl);
            timing_totals_.edge_detection_ns += elapsed_nanoseconds(queries[3], queries[4], gl);
            for (std::uint32_t iteration = 0; iteration < batch.iteration_count; ++iteration) {
                timing_totals_.edge_correction_ns += elapsed_nanoseconds(queries[5 + iteration * 2u], queries[6 + iteration * 2u], gl);
            }
        }
        timing_totals_.substep_count += batch.substep_count;
    }
    if (should_print_timings_ && !has_pending_results && timing_totals_.substep_count > 0u) {
        print_timings();
        should_print_timings_ = false;
    }
}

void SimulationPipeline::reset_timings()
{
    ++motion_generation_;
    timing_totals_ = {};
    should_print_timings_ = false;
}

SimulationPipeline::TimingBatch& SimulationPipeline::begin_timing_batch(QOpenGLFunctions_4_5_Core& gl)
{
    auto available = std::find_if(timing_batches_.begin(),
                                  timing_batches_.end(),
                                  [](const TimingBatch& batch) { return !batch.is_pending; });
    if (available == timing_batches_.end()) {
        timing_batches_.emplace_back();
        available = timing_batches_.end() - 1;
    }
    TimingBatch& batch = *available;
    const std::size_t query_count = params_.step.substep_count * (fixed_timestamps_per_substep + params_.step.iteration_count * 2u);
    if (batch.queries.size() != query_count) {
        gl.glDeleteQueries(static_cast<GLsizei>(batch.queries.size()), batch.queries.data());
        batch.queries.resize(query_count);
        gl.glGenQueries(static_cast<GLsizei>(query_count), batch.queries.data());
    }
    batch.substep_count = params_.step.substep_count;
    batch.iteration_count = params_.step.iteration_count;
    batch.motion_generation = motion_generation_;
    batch.is_pending = true;
    return batch;
}

void SimulationPipeline::print_timings() const
{
    const double scale = 1.0 / (static_cast<double>(timing_totals_.substep_count) * 1000000.0);
    std::printf("EE detect: %.3f ms | EE correction: %.3f ms | Cloth bounds: %.3f ms | Substep: %.3f ms\n",
                timing_totals_.edge_detection_ns * scale,
                timing_totals_.edge_correction_ns * scale,
                timing_totals_.cloth_bounds_ns * scale,
                timing_totals_.substep_ns * scale);
    std::fflush(stdout);
}

// Accessors
bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

// Release
void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    for (const auto& batch : timing_batches_) {
        gl.glDeleteQueries(static_cast<GLsizei>(batch.queries.size()), batch.queries.data());
    }
    timing_batches_.clear();
    reset_timings();
    garment_prefit_solver_.release(gl);
    cloth_cloth_collision_solver_.release(gl);
    cloth_body_collision_solver_.release(gl);
    collision_detector_.release(gl);
    ground_collision_solver_.release(gl);
    attachment_constraint_solver_.release(gl);
    bending_constraint_solver_.release(gl);
    stretch_constraint_solver_.release(gl);
    cloth_integrator_.release(gl);
    substep_dt_ = 0.0f;
    initialized_ = false;
}
