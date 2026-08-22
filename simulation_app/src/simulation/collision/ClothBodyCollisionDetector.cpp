#include "simulation/collision/ClothBodyCollisionDetector.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t collision_candidate_detect_local_size = 128;
constexpr std::uint32_t collision_candidate_accumulate_local_size = 128;

namespace cloth_vertex_body_face_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint body_triangle_bounds = 2;
constexpr GLuint body_triangle_bvh = 3;
constexpr GLuint candidates = 4;
constexpr GLuint candidate_count = 5;
constexpr GLuint overflow_count = 6;
}

namespace cloth_edge_body_edge_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_edges = 2;
constexpr GLuint body_edge_bounds = 3;
constexpr GLuint body_edge_bvh = 4;
constexpr GLuint candidates = 7;
constexpr GLuint candidate_count = 8;
constexpr GLuint overflow_count = 9;
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
constexpr GLuint overflow_count = 9;
}

namespace dispatch_size_binding {
constexpr GLuint candidate_count = 0;
constexpr GLuint dispatch_size = 1;
}
}

ClothBodyCollisionDetector::ClothBodyCollisionDetector(float detection_distance)
    : detection_distance_(detection_distance)
{}

bool ClothBodyCollisionDetector::is_initialized() const
{
    return has_programs();
}

void ClothBodyCollisionDetector::initialize(const std::filesystem::path& shader_dir,
                                            QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";
    cloth_vertex_body_face_.program =
        load_compute_program(collision_shader_dir / "cloth_vertex_body_face_detect.comp",
                             "Cloth vertex/body face collision candidate detection",
                             gl);
    cloth_edge_body_edge_.program =
        load_compute_program(collision_shader_dir / "cloth_edge_body_edge_detect.comp",
                             "Cloth edge/body edge collision candidate detection",
                             gl);
    cloth_face_body_vertex_.program =
        load_compute_program(collision_shader_dir / "body_vertex_cloth_face_detect.comp",
                             "Cloth face/body vertex collision candidate detection",
                             gl);
    dispatch_size_.program = load_compute_program(collision_shader_dir / "collision_dispatch_size.comp",
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

    dispatch_size_.max_candidates = gl.glGetUniformLocation(dispatch_size_.program, "uMaxCandidateCount");
    dispatch_size_.local_size = gl.glGetUniformLocation(dispatch_size_.program, "uLocalSize");

    if (cloth_vertex_body_face_.item_count < 0 ||
        cloth_vertex_body_face_.max_candidates < 0 ||
        cloth_edge_body_edge_.item_count < 0 ||
        cloth_edge_body_edge_.max_candidates < 0 ||
        cloth_face_body_vertex_.item_count < 0 ||
        cloth_face_body_vertex_.max_candidates < 0 ||
        dispatch_size_.max_candidates < 0 ||
        dispatch_size_.local_size < 0) {
        throw std::runtime_error(
            "Cloth-body collision candidate detection compute shader missing required uniforms.");
    }
}

bool ClothBodyCollisionDetector::can_detect(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           views.stretch_constraints.edge_index_buffer != 0 &&
           views.stretch_constraints.constraint_count != 0 &&
           is_valid_character_mesh_topology_resource(views.body_topology) &&
           is_valid_character_vertex_buffer_view(views.body_vertices) &&
           is_valid_bvh_buffer_view(views.body_triangle_bvh) &&
           is_valid_bvh_buffer_view(views.body_vertex_bvh) &&
           is_valid_bvh_buffer_view(views.body_edge_bvh) &&
           is_valid_collision_candidate_buffer_view(views.collision_candidates) &&
           detection_distance_ > 0.0f;
}

void ClothBodyCollisionDetector::detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    clear_collision_candidate_counts(views.collision_candidates.cloth_vertex_body_face, gl);
    clear_collision_candidate_counts(views.collision_candidates.cloth_edge_body_edge, gl);
    clear_collision_candidate_counts(views.collision_candidates.cloth_face_body_vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_vertex_body_face_collision_candidates(views.cloth_motion,
                                                       views.body_triangle_bvh,
                                                       views.collision_candidates.cloth_vertex_body_face,
                                                       gl);
    build_dispatch_size(views.collision_candidates.cloth_vertex_body_face, gl);
    detect_cloth_edge_body_edge_collision_candidates(views.cloth_motion,
                                                     views.stretch_constraints,
                                                     views.body_edge_bvh,
                                                     views.collision_candidates.cloth_edge_body_edge,
                                                     gl);
    build_dispatch_size(views.collision_candidates.cloth_edge_body_edge, gl);
    detect_cloth_face_body_vertex_collision_candidates(views.cloth_motion,
                                                       views.cloth_topology,
                                                       views.body_topology.bvh_vertex_index_buffer,
                                                       views.body_vertex_bvh,
                                                       views.collision_candidates.cloth_face_body_vertex,
                                                       gl);
    build_dispatch_size(views.collision_candidates.cloth_face_body_vertex, gl);
}

void ClothBodyCollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_vertex_body_face_.program);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_face_body_vertex_.program);
    gl.glDeleteProgram(dispatch_size_.program);

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    cloth_face_body_vertex_ = {};
    dispatch_size_ = {};
}

void ClothBodyCollisionDetector::detect_cloth_vertex_body_face_collision_candidates(
    const ClothMotionBufferView& motion_view,
    const BvhBufferView& body_triangle_bvh,
    const CollisionCandidateBuffer& collision_candidates,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_vertex_body_face_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::cloth_current,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::cloth_previous,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::body_triangle_bounds,
                        body_triangle_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::body_triangle_bvh,
                        body_triangle_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_vertex_body_face_binding::overflow_count,
                        collision_candidates.overflow_count);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program,
                           cloth_vertex_body_face_.item_count,
                           motion_view.vertex_count);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program,
                           cloth_vertex_body_face_.max_candidates,
                           collision_candidates.capacity);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, collision_candidate_detect_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionDetector::detect_cloth_edge_body_edge_collision_candidates(
    const ClothMotionBufferView& motion_view,
    const DistanceConstraintBufferView& cloth_edges,
    const BvhBufferView& body_edge_bvh,
    const CollisionCandidateBuffer& collision_candidates,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_edge_body_edge_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_current,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_previous,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::cloth_edges,
                        cloth_edges.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::body_edge_bounds,
                        body_edge_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::body_edge_bvh,
                        body_edge_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_edge_body_edge_binding::overflow_count,
                        collision_candidates.overflow_count);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program,
                           cloth_edge_body_edge_.item_count,
                           cloth_edges.constraint_count);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program,
                           cloth_edge_body_edge_.max_candidates,
                           collision_candidates.capacity);
    gl.glDispatchCompute(
        compute_group_count(cloth_edges.constraint_count, collision_candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionDetector::detect_cloth_face_body_vertex_collision_candidates(
    const ClothMotionBufferView& motion_view,
    const ClothMeshTopologyResources& cloth_topology,
    GLuint body_vertex_index_buffer,
    const BvhBufferView& body_vertex_bvh,
    const CollisionCandidateBuffer& collision_candidates,
    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(cloth_face_body_vertex_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_current,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_previous,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::cloth_triangles,
                        cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_indices,
                        body_vertex_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_bvh,
                        body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::body_vertex_bounds,
                        body_vertex_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_face_body_vertex_binding::overflow_count,
                        collision_candidates.overflow_count);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program,
                           cloth_face_body_vertex_.item_count,
                           cloth_topology.triangle_count);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program,
                           cloth_face_body_vertex_.max_candidates,
                           collision_candidates.capacity);
    gl.glDispatchCompute(
        compute_group_count(cloth_topology.triangle_count, collision_candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothBodyCollisionDetector::build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::dispatch_size,
                        collision_candidates.dispatch_size);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.max_candidates,
                           collision_candidates.capacity);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.local_size,
                           collision_candidate_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

bool ClothBodyCollisionDetector::has_programs() const
{
    return cloth_vertex_body_face_.program != 0 &&
           cloth_edge_body_edge_.program != 0 &&
           cloth_face_body_vertex_.program != 0 &&
           dispatch_size_.program != 0;
}
