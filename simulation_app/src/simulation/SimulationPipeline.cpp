#include "simulation/SimulationPipeline.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SceneState.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <glm/vec3.hpp>

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

    for (TimingSample& sample : timing_samples_) {
        gl.glGenQueries(static_cast<GLsizei>(sample.queries.size()), sample.queries.data());
        sample.distance_queries.resize(params_.step.iteration_count);
        for (auto& queries : sample.distance_queries) {
            gl.glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
        }
        sample.collision_queries.resize(params_.step.iteration_count);
        for (auto& queries : sample.collision_queries) {
            gl.glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
        }
    }

    initialized_ = true;
}

// Simulation
void SimulationPipeline::prefit_garments(SceneGpuState& gpu_state,
                                         const std::vector<const GarmentObject*>& garments,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());

    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();

    auto& queries = prefit_queries_.emplace_back();
    gl.glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
    gl.glQueryCounter(queries[0], GL_TIMESTAMP);

    // Garment pre-fit
    for (const GarmentObject* garment : garments) {
        for (std::uint32_t iteration = 0; iteration < params_.prefit.iteration_count; ++iteration) {
            garment_prefit_solver_.solve(cloth_state, garment->layer, gl);
        }
    }
    gl.glQueryCounter(queries[1], GL_TIMESTAMP);
    gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);

    // Initial cloth-cloth collision
    if (cloth_state.has_multiple_garments()) {
        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            gpu_state.update_cloth_bvh_bounds(params_.collisions.cloth.initial_detection_distance, gl);
            collision_detector_.detect_prefit(gpu_state, gl);
            cloth_cloth_collision_solver_.solve_initial(gpu_state, gl);
            gpu_state.cloth_gpu_state().copy_current_positions_to_previous(gl);
        }
        cloth_cloth_collision_solver_.update_body_surface_mapping(gpu_state, gl);
    }

    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::step(SceneState& scene,
                              SceneGpuState& gpu_state,
                              std::uint32_t motion_step_index,
                              QOpenGLFunctions_4_5_Core& gl)
{
    assert(is_initialized());

    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    cloth_cloth_collision_solver_.update_body_surface_mapping(gpu_state, gl);

    // The converter prepends 60 shape-transition and 30 pose-transition frames.
    constexpr std::uint32_t motion_intro_frame_count = 90u;
    const std::uint32_t frame_index = scene.motion_frame_index();
    const bool is_motion_frame = frame_index >= motion_intro_frame_count &&
                                 frame_index + 1u < scene.character_motion().frame_count;

    for (std::uint32_t substep = 0; substep < params_.step.substep_count; ++substep) {
        TimingSample* sample = is_motion_frame ? begin_timing_sample(frame_index) : nullptr;
        record_timestamp(sample, SubstepBegin, gl);
        update_character_motion(scene,
                                gpu_state,
                                motion_step_index,
                                substep + 1u,
                                gl,
                                sample ? sample->queries.data() + BodyBoundsBegin : nullptr);
        integrate_cloth(scene, cloth_state, gl);

        record_timestamp(sample, ClothBoundsBegin, gl);
        gpu_state.update_cloth_bvh_bounds(params_.collisions.cloth.detection_distance, gl);
        record_timestamp(sample, ClothBoundsEnd, gl);
        record_timestamp(sample, DetectBegin, gl);
        collision_detector_.detect(gpu_state, gl);
        record_timestamp(sample, DetectEnd, gl);

        for (std::uint32_t iteration = 0; iteration < params_.step.iteration_count; ++iteration) {
            if (sample) {
                gl.glQueryCounter(sample->distance_queries[iteration][0], GL_TIMESTAMP);
            }
            stretch_constraint_solver_.solve(cloth_state, gl);
            bending_constraint_solver_.solve(cloth_state, gl);
            if (sample) {
                gl.glQueryCounter(sample->distance_queries[iteration][1], GL_TIMESTAMP);
            }
            attachment_constraint_solver_.solve(cloth_state, gl);
            if (sample) {
                gl.glQueryCounter(sample->collision_queries[iteration][0], GL_TIMESTAMP);
            }
            cloth_body_collision_solver_.solve(gpu_state, gl);
            cloth_cloth_collision_solver_.solve(gpu_state, gl);
            ground_collision_solver_.solve(cloth_state, gl);
            if (sample) {
                gl.glQueryCounter(sample->collision_queries[iteration][1], GL_TIMESTAMP);
            }
        }
        record_timestamp(sample, SubstepEnd, gl);
    }

    gpu_state.update_cloth_normals(gl);
}

void SimulationPipeline::update_character_motion(SceneState& scene,
                                                 SceneGpuState& gpu_state,
                                                 std::uint32_t motion_step_index,
                                                 std::uint32_t substep,
                                                 QOpenGLFunctions_4_5_Core& gl,
                                                 const GLuint* body_bounds_queries) const
{
    const float motion_frame_position = params_.step.motion_frame_position(motion_step_index, substep);
    const float frame_alpha = scene.motion_frame_alpha(motion_frame_position);
    scene.update_reference_frame_kinematics(frame_alpha, substep_dt_);
    gpu_state.update_character_pose(scene, frame_alpha, gl, body_bounds_queries);
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

// Motion timing
void SimulationPipeline::start_motion_timing(const std::filesystem::path& motion_path)
{
    finish_motion_timing();
    motion_timings_.push_back(MotionTiming{motion_path.filename().u8string()});
}

void SimulationPipeline::finish_motion_timing()
{
    if (!motion_timings_.empty()) {
        motion_timings_.back().is_finished = true;
    }
}

void SimulationPipeline::collect_motion_timing(QOpenGLFunctions_4_5_Core& gl, bool wait_for_results)
{
    collect_prefit_timings(gl, wait_for_results);
    while (pending_timing_samples_ > 0) {
        TimingSample& sample = timing_samples_[timing_read_index_];
        if (!wait_for_results) {
            GLint is_available = GL_FALSE;
            gl.glGetQueryObjectiv(sample.queries[SubstepEnd], GL_QUERY_RESULT_AVAILABLE, &is_available);
            if (is_available == GL_FALSE) {
                break;
            }
        }

        std::array<GLuint64, TimingPointCount> timestamps{};
        for (std::size_t index = 0; index < timestamps.size(); ++index) {
            gl.glGetQueryObjectui64v(sample.queries[index], GL_QUERY_RESULT, &timestamps[index]);
        }

        MotionTiming& motion = *sample.motion;
        motion.gpu_ns[0] += static_cast<double>(timestamps[ClothBoundsEnd] - timestamps[ClothBoundsBegin]);
        motion.gpu_ns[1] += static_cast<double>(timestamps[DetectEnd] - timestamps[DetectBegin]);
        motion.gpu_ns[2] += static_cast<double>(timestamps[BodyBoundsEnd] - timestamps[BodyBoundsBegin]);
        motion.gpu_ns[3] += static_cast<double>(timestamps[SubstepEnd] - timestamps[SubstepBegin]);
        for (const auto& queries : sample.distance_queries) {
            std::array<GLuint64, 2> distance_timestamps{};
            gl.glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &distance_timestamps[0]);
            gl.glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &distance_timestamps[1]);
            motion.gpu_ns[4] += static_cast<double>(distance_timestamps[1] - distance_timestamps[0]);
        }
        for (const auto& queries : sample.collision_queries) {
            std::array<GLuint64, 2> collision_timestamps{};
            gl.glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &collision_timestamps[0]);
            gl.glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &collision_timestamps[1]);
            motion.gpu_ns[5] += static_cast<double>(collision_timestamps[1] - collision_timestamps[0]);
        }
        ++motion.substeps;
        --motion.pending_samples;
        sample.motion = nullptr;
        timing_read_index_ = (timing_read_index_ + 1u) % timing_samples_.size();
        --pending_timing_samples_;
    }
    print_finished_motion_timings();
}

SimulationPipeline::TimingSample* SimulationPipeline::begin_timing_sample(std::uint32_t frame_index)
{
    if (motion_timings_.empty() || motion_timings_.back().is_finished) {
        return nullptr;
    }

    MotionTiming& motion = motion_timings_.back();
    if (motion.substeps + motion.pending_samples + motion.skipped_substeps == 0u) {
        motion.first_frame = frame_index;
    }
    motion.last_frame = frame_index + 1u;
    if (pending_timing_samples_ == timing_samples_.size()) {
        ++motion.skipped_substeps;
        return nullptr;
    }

    const std::size_t write_index = (timing_read_index_ + pending_timing_samples_) % timing_samples_.size();
    TimingSample& sample = timing_samples_[write_index];
    sample.motion = &motion;
    ++motion.pending_samples;
    ++pending_timing_samples_;
    return &sample;
}

void SimulationPipeline::record_timestamp(const TimingSample* sample,
                                          TimingPoint point,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    if (sample) {
        gl.glQueryCounter(sample->queries[point], GL_TIMESTAMP);
    }
}

void SimulationPipeline::print_finished_motion_timings()
{
    while (!motion_timings_.empty() &&
           motion_timings_.front().is_finished &&
           motion_timings_.front().pending_samples == 0u) {
        const MotionTiming& motion = motion_timings_.front();
        if (motion.substeps > 0u || motion.skipped_substeps > 0u) {
            std::ostringstream output;
            output << "\n[Motion timing] " << motion.name << "\n  asset frames: " << motion.first_frame
                   << " -> " << motion.last_frame << "\n  measured substeps: " << motion.substeps
                   << ", skipped (query pool full): " << motion.skipped_substeps << '\n';
            if (motion.substeps > 0u) {
                const double divisor = static_cast<double>(motion.substeps) * 1.0e6;
                output << std::fixed << std::setprecision(6) << "  mean ms/substep:\n"
                       << "    cloth bound update:        " << motion.gpu_ns[0] / divisor << '\n'
                       << "    detect:                    " << motion.gpu_ns[1] / divisor << '\n'
                       << "    body bound update:         " << motion.gpu_ns[2] / divisor << '\n'
                       << "    distance constraint solve: " << motion.gpu_ns[4] / divisor << '\n'
                       << "    collision solve:           " << motion.gpu_ns[5] / divisor << '\n'
                       << "    substep:                   " << motion.gpu_ns[3] / divisor << '\n';
            }
            std::cout << output.str() << std::flush;
        }
        motion_timings_.pop_front();
    }
}

// Prefit timing
void SimulationPipeline::collect_prefit_timings(QOpenGLFunctions_4_5_Core& gl, bool wait_for_results)
{
    while (!prefit_queries_.empty()) {
        const auto& queries = prefit_queries_.front();
        if (!wait_for_results) {
            GLint is_available = GL_FALSE;
            gl.glGetQueryObjectiv(queries.back(), GL_QUERY_RESULT_AVAILABLE, &is_available);
            if (is_available == GL_FALSE) {
                break;
            }
        }

        std::array<GLuint64, 2> timestamps{};
        for (std::size_t index = 0; index < timestamps.size(); ++index) {
            gl.glGetQueryObjectui64v(queries[index], GL_QUERY_RESULT, &timestamps[index]);
        }

        std::ostringstream output;
        output << "\n[Prefit timing]\n"
               << std::fixed << std::setprecision(6)
               << "  garment prefit: " << static_cast<double>(timestamps[1] - timestamps[0]) / 1.0e6
               << " ms\n";
        std::cout << output.str() << std::flush;
        gl.glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        prefit_queries_.pop_front();
    }
}

// Accessors
bool SimulationPipeline::is_initialized() const
{
    return initialized_;
}

// Release
void SimulationPipeline::release(QOpenGLFunctions_4_5_Core& gl)
{
    finish_motion_timing();
    // Playback never waits for queries; drain the remaining samples only at teardown.
    collect_motion_timing(gl, true);
    for (TimingSample& sample : timing_samples_) {
        gl.glDeleteQueries(static_cast<GLsizei>(sample.queries.size()), sample.queries.data());
        for (const auto& queries : sample.distance_queries) {
            gl.glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        }
        for (const auto& queries : sample.collision_queries) {
            gl.glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        }
        sample = {};
    }
    timing_read_index_ = 0;
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
