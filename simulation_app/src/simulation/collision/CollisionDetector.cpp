#include "simulation/collision/CollisionDetector.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <array>
#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
}

// Initialization

void CollisionDetector::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";
    cloth_vertex_body_face_.program =
        load_compute_program(collision_shader_dir / "cloth_body" / "cloth_vertex_body_face_detect.comp",
                             "Cloth vertex/body face collision candidate detection",
                             gl);
    cloth_edge_body_edge_.program =
        load_compute_program(collision_shader_dir / "cloth_body" / "cloth_edge_body_edge_detect.comp",
                             "Cloth edge/body edge collision candidate detection",
                             gl);
    cloth_face_body_vertex_.program =
        load_compute_program(collision_shader_dir / "cloth_body" / "body_vertex_cloth_face_detect.comp",
                             "Cloth face/body vertex collision candidate detection",
                             gl);
    cloth_cloth_vertex_face_.program =
        load_compute_program(collision_shader_dir / "cloth_cloth" / "vertex_face_detect.comp",
                             "Cloth-cloth vertex-face candidate detection",
                             gl);
    dispatch_size_.program = load_compute_program(collision_shader_dir / "dispatch_size.comp",
                                                  "Collision candidate dispatch size",
                                                  gl);

    cloth_vertex_body_face_.item_count =
        gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uClothVertexCount");
    cloth_vertex_body_face_.max_candidates =
        gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uMaxCandidateCount");

    cloth_edge_body_edge_.item_count = gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uEdgeCount");
    cloth_edge_body_edge_.max_candidates =
        gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uMaxCandidateCount");

    cloth_face_body_vertex_.item_count =
        gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uTriangleCount");
    cloth_face_body_vertex_.max_candidates =
        gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uMaxCandidateCount");

    cloth_cloth_vertex_face_.upper_vertex_offset =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uUpperVertexOffset");
    cloth_cloth_vertex_face_.upper_vertex_count =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uUpperVertexCount");
    cloth_cloth_vertex_face_.upper_bvh_root =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uUpperBvhRoot");
    cloth_cloth_vertex_face_.lower_vertex_offset =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uLowerVertexOffset");
    cloth_cloth_vertex_face_.lower_vertex_count =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uLowerVertexCount");
    cloth_cloth_vertex_face_.lower_bvh_root =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uLowerBvhRoot");
    cloth_cloth_vertex_face_.max_candidates =
        gl.glGetUniformLocation(cloth_cloth_vertex_face_.program, "uMaxCandidateCount");

    dispatch_size_.max_candidates = gl.glGetUniformLocation(dispatch_size_.program, "uMaxCandidateCount");

    if (cloth_vertex_body_face_.item_count < 0 ||
        cloth_vertex_body_face_.max_candidates < 0 ||
        cloth_edge_body_edge_.item_count < 0 ||
        cloth_edge_body_edge_.max_candidates < 0 ||
        cloth_face_body_vertex_.item_count < 0 ||
        cloth_face_body_vertex_.max_candidates < 0 ||
        cloth_cloth_vertex_face_.upper_vertex_offset < 0 ||
        cloth_cloth_vertex_face_.upper_vertex_count < 0 ||
        cloth_cloth_vertex_face_.upper_bvh_root < 0 ||
        cloth_cloth_vertex_face_.lower_vertex_offset < 0 ||
        cloth_cloth_vertex_face_.lower_vertex_count < 0 ||
        cloth_cloth_vertex_face_.lower_bvh_root < 0 ||
        cloth_cloth_vertex_face_.max_candidates < 0 ||
        dispatch_size_.max_candidates < 0) {
        throw std::runtime_error("Collision candidate detection compute shader missing required uniforms.");
    }
}

// Detection

void CollisionDetector::detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    clear_collision_candidate_counts(views.collision.cloth_vertex_body_face, gl);
    clear_collision_candidate_counts(views.collision.cloth_edge_body_edge, gl);
    clear_collision_candidate_counts(views.collision.cloth_face_body_vertex, gl);
    if (views.has_multiple_garments()) {
        clear_collision_candidate_counts(views.collision.cloth_cloth_vertex_face, gl);
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_vertex_body_face(views, gl);
    build_dispatch_size(views.collision.cloth_vertex_body_face, gl);

    detect_cloth_edge_body_edge(views, gl);
    build_dispatch_size(views.collision.cloth_edge_body_edge, gl);

    detect_cloth_face_body_vertex(views, gl);
    build_dispatch_size(views.collision.cloth_face_body_vertex, gl);

    if (views.has_multiple_garments()) {
        detect_cloth_cloth_vertex_face(views, gl);
        build_dispatch_size(views.collision.cloth_cloth_vertex_face, gl);
    }
}

void CollisionDetector::detect_prefit(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect_prefit(views));

    clear_collision_candidate_counts(views.collision.cloth_cloth_vertex_face, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_cloth_vertex_face(views, gl);
    build_dispatch_size(views.collision.cloth_cloth_vertex_face, gl);
}

// Detection

void CollisionDetector::detect_cloth_vertex_body_face(const SimulationGpuView& views,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_vertex_body_face_;
    const auto& candidates = views.collision.cloth_vertex_body_face;
    const std::array<GLuint, 6> buffers{
        views.cloth_motion.current_position_buffer,
        views.cloth_motion.previous_position_buffer,
        views.body_triangle_bvh.bounds_buffer,
        views.body_triangle_bvh.node_buffer,
        candidates.candidate_buffer,
        candidates.count_buffer,
    };

    gl.glUseProgram(shader.program);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, buffers.size(), buffers.data());
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.cloth_motion.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, candidate_detect_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_edge_body_edge(const SimulationGpuView& views,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_edge_body_edge_;
    const auto& candidates = views.collision.cloth_edge_body_edge;
    const std::array<GLuint, 7> buffers{
        views.cloth_motion.current_position_buffer,
        views.cloth_motion.previous_position_buffer,
        views.stretch_constraints.edge_index_buffer,
        views.body_edge_bvh.bounds_buffer,
        views.body_edge_bvh.node_buffer,
        candidates.candidate_buffer,
        candidates.count_buffer,
    };

    gl.glUseProgram(shader.program);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, buffers.size(), buffers.data());
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.stretch_constraints.constraint_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.stretch_constraints.constraint_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_face_body_vertex(const SimulationGpuView& views,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_face_body_vertex_;
    const auto& candidates = views.collision.cloth_face_body_vertex;
    const std::array<GLuint, 8> buffers{
        views.cloth_motion.current_position_buffer,
        views.cloth_motion.previous_position_buffer,
        views.cloth_topology.triangle_index_buffer,
        views.body_topology.bvh_vertex_index_buffer,
        views.body_vertex_bvh.node_buffer,
        views.body_vertex_bvh.bounds_buffer,
        candidates.candidate_buffer,
        candidates.count_buffer,
    };

    gl.glUseProgram(shader.program);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, buffers.size(), buffers.data());
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.cloth_topology.triangle_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.cloth_topology.triangle_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_cloth_vertex_face(const SimulationGpuView& views,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_cloth_vertex_face_;
    const auto& candidates = views.collision.cloth_cloth_vertex_face;
    const GarmentBufferState& upper = views.garment_buffer_states[GarmentLayer::Upper];
    const GarmentBufferState& lower = views.garment_buffer_states[GarmentLayer::Lower];
    const std::array<GLuint, 6> buffers{
        views.cloth_motion.current_position_buffer,
        views.cloth_motion.previous_position_buffer,
        views.cloth_bvh.bounds_buffer,
        views.cloth_bvh.node_buffer,
        candidates.candidate_buffer,
        candidates.count_buffer,
    };

    gl.glUseProgram(shader.program);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, buffers.size(), buffers.data());
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glProgramUniform1ui(shader.program, shader.upper_vertex_offset, upper.vertex_start_index);
    gl.glProgramUniform1ui(shader.program, shader.upper_vertex_count, upper.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.upper_bvh_root, upper.bvh_level_offsets.front());
    gl.glProgramUniform1ui(shader.program, shader.lower_vertex_offset, lower.vertex_start_index);
    gl.glProgramUniform1ui(shader.program, shader.lower_vertex_count, lower.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.lower_bvh_root, lower.bvh_level_offsets.front());

    const std::uint32_t query_vertex_count = upper.vertex_count + lower.vertex_count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::build_dispatch_size(const CollisionCandidateBuffers& collision_candidates,
                                            QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = dispatch_size_;
    const std::array<GLuint, 2> buffers{
        collision_candidates.count_buffer,
        collision_candidates.dispatch_size_buffer,
    };

    gl.glUseProgram(shader.program);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, 0, buffers.size(), buffers.data());
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, collision_candidates.max_pairs);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

// Validation

bool CollisionDetector::is_initialized() const
{
    return cloth_vertex_body_face_.program != 0 &&
           cloth_edge_body_edge_.program != 0 &&
           cloth_face_body_vertex_.program != 0 &&
           cloth_cloth_vertex_face_.program != 0 &&
           dispatch_size_.program != 0;
}

bool CollisionDetector::can_detect(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           views.stretch_constraints.edge_index_buffer != 0 &&
           views.stretch_constraints.constraint_count != 0 &&
           views.body_topology.bvh_vertex_index_buffer != 0 &&
           is_valid_bvh_buffer_view(views.body_triangle_bvh) &&
           is_valid_bvh_buffer_view(views.body_vertex_bvh) &&
           is_valid_bvh_buffer_view(views.body_edge_bvh) &&
           is_valid_collision_candidate_buffer_view(views.collision) &&
           (!views.has_multiple_garments() || can_detect_prefit(views));
}

bool CollisionDetector::can_detect_prefit(const SimulationGpuView& views) const
{
    return is_initialized() &&
           views.has_multiple_garments() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           is_valid_bvh_buffer_view(views.cloth_bvh) &&
           views.cloth_topology.vertex_count == views.cloth_motion.vertex_count &&
           is_valid_cloth_cloth_candidate_buffer_view(views.collision);
}

// Release

void CollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_vertex_body_face_.program);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_face_body_vertex_.program);
    gl.glDeleteProgram(cloth_cloth_vertex_face_.program);
    gl.glDeleteProgram(dispatch_size_.program);

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    cloth_face_body_vertex_ = {};
    cloth_cloth_vertex_face_ = {};
    dispatch_size_ = {};
}
