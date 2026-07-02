#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint cloth_current_positions_binding = 0;
constexpr GLuint cloth_previous_positions_binding = 1;
constexpr GLuint cloth_triangle_indices_binding = 2;
constexpr GLuint colorized_cloth_triangle_ids_binding = 3;
constexpr GLuint body_previous_positions_binding = 4;
constexpr GLuint body_current_positions_binding = 5;
constexpr GLuint body_vertex_normals_binding = 6;
constexpr GLuint body_vertex_ids_binding = 7;
constexpr GLuint body_vertex_bvh_nodes_binding = 8;
constexpr GLuint collision_states_binding = 9;
constexpr GLuint contact_normals_binding = 10;
constexpr std::uint32_t body_vertex_cloth_face_collision_local_size = 128;
#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
constexpr std::uint32_t gpu_timing_log_interval = 512u;
constexpr double gpu_timing_nanoseconds_to_milliseconds = 1.0e-6;
#endif
}

bool BodyVertexClothFaceCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool BodyVertexClothFaceCollisionSolver::initialize(const std::filesystem::path& shader_path,
                                                    float collision_thickness,
                                                    float max_correction_length,
                                                    QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Body vertex/cloth face collision", gl);
    if (program_ == 0) {
        return false;
    }

    triangle_color_offset_location_ = gl.glGetUniformLocation(program_, "uTriangleColorOffset");
    triangle_dispatch_count_location_ = gl.glGetUniformLocation(program_, "uTriangleDispatchCount");
    root_node_index_location_ = gl.glGetUniformLocation(program_, "uRootNodeIndex");
    max_contacts_per_vertex_location_ = gl.glGetUniformLocation(program_, "uMaxContactsPerVertex");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");
    max_correction_length_location_ = gl.glGetUniformLocation(program_, "uMaxCorrectionLength");

    if (triangle_color_offset_location_ < 0 ||
        triangle_dispatch_count_location_ < 0 ||
        root_node_index_location_ < 0 ||
        max_contacts_per_vertex_location_ < 0 ||
        collision_thickness_location_ < 0 ||
        max_correction_length_location_ < 0) {
        std::cerr << "Body vertex/cloth face collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    max_correction_length_ = max_correction_length;
#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    initialize_gpu_timing_queries(gl);
#endif
    return true;
}

bool BodyVertexClothFaceCollisionSolver::can_solve(const ClothMotionBufferView& motion_view,
                                                   const ClothCollisionStateBufferView& collision_view,
                                                   const ClothMeshTopologyResources& cloth_topology,
                                                   const ClothTriangleColorView& triangle_color_view,
                                                   const CharacterVertexBufferView& character_vertex_view,
                                                   const BodyVertexBvhResources& body_vertex_bvh) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_collision_state_view(collision_view) &&
           motion_view.vertex_count == collision_view.vertex_count &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           is_valid_cloth_triangle_color_view(triangle_color_view) &&
           cloth_topology.triangle_count == triangle_color_view.triangle_count &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_body_vertex_bvh_resource(body_vertex_bvh) &&
           collision_thickness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

void BodyVertexClothFaceCollisionSolver::solve(const ClothMotionBufferView& motion_view,
                                               const ClothCollisionStateBufferView& collision_view,
                                               const ClothMeshTopologyResources& cloth_topology,
                                               const ClothTriangleColorView& triangle_color_view,
                                               const CharacterVertexBufferView& character_vertex_view,
                                               const BodyVertexBvhResources& body_vertex_bvh,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(motion_view,
                     collision_view,
                     cloth_topology,
                     triangle_color_view,
                     character_vertex_view,
                     body_vertex_bvh));

#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    const bool gpu_timing_started = begin_gpu_timing(gl);
#endif

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_current_positions_binding, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_previous_positions_binding, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_triangle_indices_binding, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, colorized_cloth_triangle_ids_binding, triangle_color_view.triangle_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_previous_positions_binding, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_current_positions_binding, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_normals_binding, character_vertex_view.vertex_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_ids_binding, body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, body_vertex_bvh_nodes_binding, body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, collision_states_binding, collision_view.collision_state_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, contact_normals_binding, collision_view.contact_normal_buffer);
    gl.glProgramUniform1ui(program_, root_node_index_location_, body_vertex_bvh.root_node_index);
    gl.glProgramUniform1ui(program_, max_contacts_per_vertex_location_, collision_view.max_contacts_per_vertex);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness_);
    gl.glProgramUniform1f(program_, max_correction_length_location_, max_correction_length_);

    for (const ElementRange& range : *triangle_color_view.color_ranges) {
        if (range.count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(program_, triangle_color_offset_location_, range.offset);
        gl.glProgramUniform1ui(program_, triangle_dispatch_count_location_, range.count);
        gl.glDispatchCompute(compute_group_count(range.count, body_vertex_cloth_face_collision_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    if (gpu_timing_started) {
        end_gpu_timing(gl);
    }
#endif
}

void BodyVertexClothFaceCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    release_gpu_timing_queries(gl);
#endif

    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    triangle_color_offset_location_ = -1;
    triangle_dispatch_count_location_ = -1;
    root_node_index_location_ = -1;
    max_contacts_per_vertex_location_ = -1;
    collision_thickness_location_ = -1;
    max_correction_length_location_ = -1;
    collision_thickness_ = 0.0f;
    max_correction_length_ = 0.0f;
}

#if CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
void BodyVertexClothFaceCollisionSolver::initialize_gpu_timing_queries(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glGenQueries(static_cast<GLsizei>(gpu_timing_queries_.size()), gpu_timing_queries_.data());
    gpu_timing_query_pending_.fill(false);
    next_gpu_timing_query_ = 0u;
    gpu_timing_active_ = false;
    gpu_timing_sample_count_ = 0u;
    gpu_timing_window_sample_count_ = 0u;
    gpu_timing_window_total_ms_ = 0.0;

    std::cerr << "[TEMP GPU TIMING] BodyVertexClothFaceCollisionSolver GL_TIME_ELAPSED enabled. "
              << "Remove blocks marked CLOTH_SIM_TEMP_BODY_VERTEX_CLOTH_FACE_GPU_TIMING after profiling.\n";
}

void BodyVertexClothFaceCollisionSolver::release_gpu_timing_queries(QOpenGLFunctions_4_5_Core& gl)
{
    if (gpu_timing_active_) {
        gl.glEndQuery(GL_TIME_ELAPSED);
        gpu_timing_active_ = false;
    }

    if (gpu_timing_queries_[0] != 0u) {
        gl.glDeleteQueries(static_cast<GLsizei>(gpu_timing_queries_.size()), gpu_timing_queries_.data());
    }

    gpu_timing_queries_.fill(0u);
    gpu_timing_query_pending_.fill(false);
    next_gpu_timing_query_ = 0u;
    gpu_timing_sample_count_ = 0u;
    gpu_timing_window_sample_count_ = 0u;
    gpu_timing_window_total_ms_ = 0.0;
}

void BodyVertexClothFaceCollisionSolver::collect_gpu_timing_results(QOpenGLFunctions_4_5_Core& gl) const
{
    for (std::size_t query_index = 0u; query_index < gpu_timing_queries_.size(); ++query_index) {
        if (!gpu_timing_query_pending_[query_index]) {
            continue;
        }

        GLuint is_available = GL_FALSE;
        gl.glGetQueryObjectuiv(gpu_timing_queries_[query_index], GL_QUERY_RESULT_AVAILABLE, &is_available);
        if (is_available == GL_FALSE) {
            continue;
        }

        GLuint64 elapsed_nanoseconds = 0u;
        gl.glGetQueryObjectui64v(gpu_timing_queries_[query_index], GL_QUERY_RESULT, &elapsed_nanoseconds);
        gpu_timing_query_pending_[query_index] = false;

        const double elapsed_ms =
            static_cast<double>(elapsed_nanoseconds) * gpu_timing_nanoseconds_to_milliseconds;
        ++gpu_timing_sample_count_;
        ++gpu_timing_window_sample_count_;
        gpu_timing_window_total_ms_ += elapsed_ms;

        if (gpu_timing_window_sample_count_ >= gpu_timing_log_interval) {
            const double average_ms =
                gpu_timing_window_total_ms_ / static_cast<double>(gpu_timing_window_sample_count_);
            std::cerr << "[TEMP GPU TIMING] BodyVertexClothFaceCollisionSolver::solve average "
                      << average_ms << " ms over " << gpu_timing_window_sample_count_
                      << " samples, total samples " << gpu_timing_sample_count_ << ".\n";
            gpu_timing_window_sample_count_ = 0u;
            gpu_timing_window_total_ms_ = 0.0;
        }
    }
}

bool BodyVertexClothFaceCollisionSolver::begin_gpu_timing(QOpenGLFunctions_4_5_Core& gl) const
{
    collect_gpu_timing_results(gl);
    if (gpu_timing_queries_[0] == 0u || gpu_timing_active_) {
        return false;
    }

    const std::size_t query_index = next_gpu_timing_query_;
    if (gpu_timing_query_pending_[query_index]) {
        return false;
    }

    gl.glBeginQuery(GL_TIME_ELAPSED, gpu_timing_queries_[query_index]);
    gpu_timing_query_pending_[query_index] = true;
    gpu_timing_active_ = true;
    next_gpu_timing_query_ = (next_gpu_timing_query_ + 1u) % gpu_timing_queries_.size();
    return true;
}

void BodyVertexClothFaceCollisionSolver::end_gpu_timing(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glEndQuery(GL_TIME_ELAPSED);
    gpu_timing_active_ = false;
}
#endif
