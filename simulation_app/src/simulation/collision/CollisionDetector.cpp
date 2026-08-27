#include "simulation/collision/CollisionDetector.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
constexpr std::uint32_t candidate_accumulate_local_size = 128u;

namespace cloth_vertex_body_face_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint body_triangle_bounds = 2;
constexpr GLuint body_triangle_bvh = 3;
constexpr GLuint candidates = 4;
constexpr GLuint candidate_count = 5;
}

namespace cloth_edge_body_edge_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_edges = 2;
constexpr GLuint body_edge_bounds = 3;
constexpr GLuint body_edge_bvh = 4;
constexpr GLuint candidates = 7;
constexpr GLuint candidate_count = 8;
}

namespace cloth_face_body_vertex_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_vertex_indices = 3;
constexpr GLuint body_vertex_bvh = 4;
constexpr GLuint body_vertex_bounds = 5;
constexpr GLuint candidates = 7;
constexpr GLuint candidate_count = 8;
}

namespace cloth_cloth_vertex_face_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint triangle_bounds = 2;
constexpr GLuint bvh_nodes = 3;
constexpr GLuint candidates = 4;
constexpr GLuint candidate_count = 5;
}

namespace dispatch_size_binding {
constexpr GLuint candidate_count = 0;
constexpr GLuint dispatch_size = 1;
}
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
    dispatch_size_.local_size = gl.glGetUniformLocation(dispatch_size_.program, "uLocalSize");

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
        dispatch_size_.max_candidates < 0 ||
        dispatch_size_.local_size < 0) {
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
    gl.glUseProgram(cloth_vertex_body_face_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::body_triangle_bounds,
                        views.body_triangle_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::body_triangle_bvh,
                        views.body_triangle_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::candidates,
                        views.collision.cloth_vertex_body_face.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::candidate_count,
                        views.collision.cloth_vertex_body_face.count_buffer);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program,
                           cloth_vertex_body_face_.item_count,
                           views.cloth_motion.vertex_count);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program,
                           cloth_vertex_body_face_.max_candidates,
                           views.collision.cloth_vertex_body_face.max_pairs);
    gl.glDispatchCompute(compute_group_count(views.cloth_motion.vertex_count, candidate_detect_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_edge_body_edge(const SimulationGpuView& views,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_edge_body_edge_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_edges,
                        views.stretch_constraints.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::body_edge_bounds,
                        views.body_edge_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::body_edge_bvh,
                        views.body_edge_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::candidates,
                        views.collision.cloth_edge_body_edge.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::candidate_count,
                        views.collision.cloth_edge_body_edge.count_buffer);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program,
                           cloth_edge_body_edge_.item_count,
                           views.stretch_constraints.constraint_count);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program,
                           cloth_edge_body_edge_.max_candidates,
                           views.collision.cloth_edge_body_edge.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.stretch_constraints.constraint_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_face_body_vertex(const SimulationGpuView& views,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_face_body_vertex_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_triangles,
                        views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_indices,
                        views.body_topology.bvh_vertex_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_bvh,
                        views.body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_bounds,
                        views.body_vertex_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::candidates,
                        views.collision.cloth_face_body_vertex.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::candidate_count,
                        views.collision.cloth_face_body_vertex.count_buffer);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program,
                           cloth_face_body_vertex_.item_count,
                           views.cloth_topology.triangle_count);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program,
                           cloth_face_body_vertex_.max_candidates,
                           views.collision.cloth_face_body_vertex.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.cloth_topology.triangle_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_cloth_vertex_face(const SimulationGpuView& views,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_cloth_vertex_face_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::triangle_bounds,
                        views.cloth_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::bvh_nodes,
                        views.cloth_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::candidates,
                        views.collision.cloth_cloth_vertex_face.candidate_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_vertex_face_binding::candidate_count,
                        views.collision.cloth_cloth_vertex_face.count_buffer);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.max_candidates,
                           views.collision.cloth_cloth_vertex_face.max_pairs);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.upper_vertex_offset,
                           views.garment_buffer_states[GarmentLayer::Upper].vertex_start_index);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.upper_vertex_count,
                           views.garment_buffer_states[GarmentLayer::Upper].vertex_count);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.upper_bvh_root,
                           views.garment_buffer_states[GarmentLayer::Upper].bvh_level_offsets.front());
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.lower_vertex_offset,
                           views.garment_buffer_states[GarmentLayer::Lower].vertex_start_index);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.lower_vertex_count,
                           views.garment_buffer_states[GarmentLayer::Lower].vertex_count);
    gl.glProgramUniform1ui(cloth_cloth_vertex_face_.program,
                           cloth_cloth_vertex_face_.lower_bvh_root,
                           views.garment_buffer_states[GarmentLayer::Lower].bvh_level_offsets.front());

    const std::uint32_t query_vertex_count = views.garment_buffer_states[GarmentLayer::Upper].vertex_count +
                                             views.garment_buffer_states[GarmentLayer::Lower].vertex_count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::build_dispatch_size(const CollisionCandidateBuffers& collision_candidates,
                                            QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::candidate_count,
                        collision_candidates.count_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::dispatch_size,
                        collision_candidates.dispatch_size_buffer);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.max_candidates,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.local_size,
                           candidate_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

// Validation

bool CollisionDetector::is_initialized() const
{
    return has_programs();
}

bool CollisionDetector::can_detect(const SimulationGpuView& views) const
{
    if (!is_initialized() ||
        !is_valid_motion_view(views.cloth_motion) ||
        !is_valid_cloth_mesh_topology_resource(views.cloth_topology) ||
        views.stretch_constraints.edge_index_buffer == 0 ||
        views.stretch_constraints.constraint_count == 0 ||
        !is_valid_character_mesh_topology_resource(views.body_topology) ||
        !is_valid_character_vertex_buffer_view(views.body_vertices) ||
        !is_valid_bvh_buffer_view(views.body_triangle_bvh) ||
        !is_valid_bvh_buffer_view(views.body_vertex_bvh) ||
        !is_valid_bvh_buffer_view(views.body_edge_bvh) ||
        !is_valid_collision_candidate_buffer_view(views.collision)) {
        return false;
    }

    return !views.has_multiple_garments() || can_detect_prefit(views);
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

bool CollisionDetector::has_programs() const
{
    return cloth_vertex_body_face_.program != 0 &&
           cloth_edge_body_edge_.program != 0 &&
           cloth_face_body_vertex_.program != 0 &&
           cloth_cloth_vertex_face_.program != 0 &&
           dispatch_size_.program != 0;
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
