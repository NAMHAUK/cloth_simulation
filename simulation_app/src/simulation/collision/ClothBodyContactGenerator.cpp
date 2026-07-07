#include "simulation/collision/ClothBodyContactGenerator.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr std::uint32_t contact_generate_local_size = 128;
constexpr std::uint32_t contact_accumulate_local_size = 128;
constexpr std::uint32_t gpu_timing_log_interval = 100u;

namespace cloth_vertex_body_face_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint body_triangle_geometry = 2;
constexpr GLuint body_triangle_bvh = 3;
constexpr GLuint pair_records = 4;
constexpr GLuint pair_count = 5;
constexpr GLuint overflow_count = 6;
}

namespace cloth_edge_body_edge_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_edges = 2;
constexpr GLuint body_edge_indices = 3;
constexpr GLuint body_edge_bvh = 4;
constexpr GLuint body_current = 5;
constexpr GLuint body_previous = 6;
constexpr GLuint pair_records = 7;
constexpr GLuint pair_count = 8;
constexpr GLuint overflow_count = 9;
}

namespace cloth_face_body_vertex_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint cloth_triangles = 2;
constexpr GLuint body_vertex_ids = 3;
constexpr GLuint body_vertex_bvh = 4;
constexpr GLuint body_current = 5;
constexpr GLuint body_previous = 6;
constexpr GLuint pair_records = 7;
constexpr GLuint pair_count = 8;
constexpr GLuint overflow_count = 9;
}

namespace dispatch_build_binding {
constexpr GLuint pair_count = 0;
constexpr GLuint dispatch_command = 1;
}
}

bool ClothBodyContactGenerator::is_initialized() const
{
    return has_programs();
}

bool ClothBodyContactGenerator::initialize(const std::filesystem::path& cloth_vertex_body_face_generate_shader_path,
                                           const std::filesystem::path& cloth_edge_body_edge_generate_shader_path,
                                           const std::filesystem::path& cloth_face_body_vertex_generate_shader_path,
                                           const std::filesystem::path& dispatch_build_shader_path,
                                           float collision_thickness,
                                           std::uint32_t ignored_body_part_mask,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    cloth_vertex_body_face_.program = load_compute_program(cloth_vertex_body_face_generate_shader_path,
                                                           "Cloth vertex/body face pair generation",
                                                           gl);
    cloth_edge_body_edge_.program = load_compute_program(cloth_edge_body_edge_generate_shader_path,
                                                         "Cloth edge/body edge pair generation",
                                                         gl);
    cloth_face_body_vertex_.program = load_compute_program(cloth_face_body_vertex_generate_shader_path,
                                                           "Cloth face/body vertex pair generation",
                                                           gl);
    dispatch_build_.program = load_compute_program(dispatch_build_shader_path,
                                                   "Contact pair dispatch command build",
                                                   gl);
    if (!has_programs()) {
        release(gl);
        return false;
    }

    cloth_vertex_body_face_.vertex_count = gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uClothVertexCount");
    cloth_vertex_body_face_.max_pairs = gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uMaxPairCount");
    cloth_vertex_body_face_.thickness = gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uCollisionThickness");
    cloth_vertex_body_face_.ignored_body_part_mask = gl.glGetUniformLocation(cloth_vertex_body_face_.program, "uIgnoredBodyPartMask");
    cloth_edge_body_edge_.edge_count = gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uEdgeCount");
    cloth_edge_body_edge_.max_pairs = gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uMaxPairCount");
    cloth_edge_body_edge_.thickness = gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uCollisionThickness");
    cloth_edge_body_edge_.ignored_body_part_mask = gl.glGetUniformLocation(cloth_edge_body_edge_.program, "uIgnoredBodyPartMask");
    cloth_face_body_vertex_.triangle_count = gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uTriangleCount");
    cloth_face_body_vertex_.max_pairs = gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uMaxPairCount");
    cloth_face_body_vertex_.thickness = gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uCollisionThickness");
    cloth_face_body_vertex_.ignored_body_part_mask = gl.glGetUniformLocation(cloth_face_body_vertex_.program, "uIgnoredBodyPartMask");
    dispatch_build_.max_pairs = gl.glGetUniformLocation(dispatch_build_.program, "uMaxPairCount");
    dispatch_build_.local_size = gl.glGetUniformLocation(dispatch_build_.program, "uLocalSize");

    if (cloth_vertex_body_face_.vertex_count < 0 ||
        cloth_vertex_body_face_.max_pairs < 0 ||
        cloth_vertex_body_face_.thickness < 0 ||
        cloth_vertex_body_face_.ignored_body_part_mask < 0 ||
        cloth_edge_body_edge_.edge_count < 0 ||
        cloth_edge_body_edge_.max_pairs < 0 ||
        cloth_edge_body_edge_.thickness < 0 ||
        cloth_edge_body_edge_.ignored_body_part_mask < 0 ||
        cloth_face_body_vertex_.triangle_count < 0 ||
        cloth_face_body_vertex_.max_pairs < 0 ||
        cloth_face_body_vertex_.thickness < 0 ||
        cloth_face_body_vertex_.ignored_body_part_mask < 0 ||
        dispatch_build_.max_pairs < 0 ||
        dispatch_build_.local_size < 0) {
        std::cerr << "Cloth-body contact generation compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    collision_thickness_ = collision_thickness;
    ignored_body_part_mask_ = ignored_body_part_mask;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    cloth_vertex_body_face_timer_.initialize("cloth vertex - body face pair generation", gpu_timing_log_interval, gl);
    cloth_edge_body_edge_timer_.initialize("cloth edge - body edge pair generation", gpu_timing_log_interval, gl);
    cloth_face_body_vertex_timer_.initialize("body vertex - cloth face pair generation", gpu_timing_log_interval, gl);
#endif
    return true;
}

bool ClothBodyContactGenerator::can_generate(const ClothMotionBufferView& motion_view,
                                             const ClothMeshTopologyResources& cloth_topology,
                                             const DistanceConstraintBufferView& cloth_edges,
                                             const CharacterVertexBufferView& character_vertex_view,
                                             const TriangleGeometryResources& character_geometry,
                                             const TriangleBvhResources& character_bvh,
                                             const VertexBvhResources& body_vertex_bvh,
                                             const EdgeBvhResources& body_edge_bvh,
                                             const CollisionContactBufferView& collision_contact_view) const
{
    return is_initialized() &&
           is_valid_motion_view(motion_view) &&
           is_valid_cloth_mesh_topology_resource(cloth_topology) &&
           cloth_edges.edge_index_buffer != 0 &&
           cloth_edges.constraint_count != 0 &&
           is_valid_character_vertex_buffer_view(character_vertex_view) &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_triangle_bvh_resource(character_bvh) &&
           is_valid_vertex_bvh_resource(body_vertex_bvh) &&
           is_valid_edge_bvh_resource(body_edge_bvh) &&
           is_valid_collision_contact_buffer_view(collision_contact_view) &&
           collision_contact_view.vertex_capacity >= motion_view.vertex_count &&
           collision_contact_view.cloth_vertex_body_face.capacity >= motion_view.vertex_count * CollisionContactBuffers::pair_capacity_multiplier &&
           collision_contact_view.cloth_edge_body_edge.capacity >= cloth_edges.constraint_count * CollisionContactBuffers::pair_capacity_multiplier &&
           collision_contact_view.cloth_face_body_vertex.capacity >= cloth_topology.triangle_count * CollisionContactBuffers::pair_capacity_multiplier &&
           collision_thickness_ > 0.0f;
}

void ClothBodyContactGenerator::generate_contacts(const ClothMotionBufferView& motion_view,
                                                  const ClothMeshTopologyResources& cloth_topology,
                                                  const DistanceConstraintBufferView& cloth_edges,
                                                  const CharacterVertexBufferView& character_vertex_view,
                                                  const TriangleGeometryResources& character_geometry,
                                                  const TriangleBvhResources& character_bvh,
                                                  const VertexBvhResources& body_vertex_bvh,
                                                  const EdgeBvhResources& body_edge_bvh,
                                                  const CollisionContactBufferView& collision_contact_view,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_generate(motion_view, cloth_topology, cloth_edges, character_vertex_view, character_geometry, character_bvh, body_vertex_bvh, body_edge_bvh, collision_contact_view));

    collision_contact_view.clear_contact_counts(gl);
    generate_cloth_vertex_body_face_contacts(motion_view,
                                             character_geometry,
                                             character_bvh,
                                             collision_contact_view.cloth_vertex_body_face,
                                             gl);
    build_dispatch_command(collision_contact_view.cloth_vertex_body_face, gl);
    generate_cloth_edge_body_edge_contacts(motion_view,
                                           cloth_edges,
                                           character_vertex_view,
                                           body_edge_bvh,
                                           collision_contact_view.cloth_edge_body_edge,
                                           gl);
    build_dispatch_command(collision_contact_view.cloth_edge_body_edge, gl);
    generate_cloth_face_body_vertex_contacts(motion_view,
                                             cloth_topology,
                                             character_vertex_view,
                                             body_vertex_bvh,
                                             collision_contact_view.cloth_face_body_vertex,
                                             gl);
    build_dispatch_command(collision_contact_view.cloth_face_body_vertex, gl);
}

void ClothBodyContactGenerator::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_vertex_body_face_.program);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_face_body_vertex_.program);
    gl.glDeleteProgram(dispatch_build_.program);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    cloth_vertex_body_face_timer_.release(gl);
    cloth_edge_body_edge_timer_.release(gl);
    cloth_face_body_vertex_timer_.release(gl);
#endif

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    cloth_face_body_vertex_ = {};
    dispatch_build_ = {};
    collision_thickness_ = 0.0f;
    ignored_body_part_mask_ = 0;
}

void ClothBodyContactGenerator::generate_cloth_vertex_body_face_contacts(const ClothMotionBufferView& motion_view,
                                                                         const TriangleGeometryResources& character_geometry,
                                                                         const TriangleBvhResources& character_bvh,
                                                                         const ContactPairBuffers& contact_pairs,
                                                                         QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_GPU_TIMING
    const bool gpu_timing_started = cloth_vertex_body_face_timer_.begin(gl);
#endif
    gl.glUseProgram(cloth_vertex_body_face_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::body_triangle_geometry, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::body_triangle_bvh, character_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::pair_records, contact_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::pair_count, contact_pairs.count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_vertex_body_face_binding::overflow_count, contact_pairs.overflow_count);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program, cloth_vertex_body_face_.vertex_count, motion_view.vertex_count);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program, cloth_vertex_body_face_.max_pairs, contact_pairs.capacity);
    gl.glProgramUniform1f(cloth_vertex_body_face_.program, cloth_vertex_body_face_.thickness, collision_thickness_);
    gl.glProgramUniform1ui(cloth_vertex_body_face_.program, cloth_vertex_body_face_.ignored_body_part_mask, ignored_body_part_mask_);
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, contact_generate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        cloth_vertex_body_face_timer_.end(gl);
    }
#endif
}

void ClothBodyContactGenerator::generate_cloth_edge_body_edge_contacts(const ClothMotionBufferView& motion_view,
                                                                       const DistanceConstraintBufferView& cloth_edges,
                                                                       const CharacterVertexBufferView& character_vertex_view,
                                                                       const EdgeBvhResources& body_edge_bvh,
                                                                       const ContactPairBuffers& contact_pairs,
                                                                       QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_GPU_TIMING
    const bool gpu_timing_started = cloth_edge_body_edge_timer_.begin(gl);
#endif
    gl.glUseProgram(cloth_edge_body_edge_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::cloth_edges, cloth_edges.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::body_edge_indices, body_edge_bvh.edge_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::body_edge_bvh, body_edge_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::body_current, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::body_previous, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::pair_records, contact_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::pair_count, contact_pairs.count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_edge_body_edge_binding::overflow_count, contact_pairs.overflow_count);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program, cloth_edge_body_edge_.edge_count, cloth_edges.constraint_count);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program, cloth_edge_body_edge_.max_pairs, contact_pairs.capacity);
    gl.glProgramUniform1f(cloth_edge_body_edge_.program, cloth_edge_body_edge_.thickness, collision_thickness_);
    gl.glProgramUniform1ui(cloth_edge_body_edge_.program, cloth_edge_body_edge_.ignored_body_part_mask, ignored_body_part_mask_);
    gl.glDispatchCompute(compute_group_count(cloth_edges.constraint_count, contact_generate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        cloth_edge_body_edge_timer_.end(gl);
    }
#endif
}

void ClothBodyContactGenerator::generate_cloth_face_body_vertex_contacts(const ClothMotionBufferView& motion_view,
                                                                         const ClothMeshTopologyResources& cloth_topology,
                                                                         const CharacterVertexBufferView& character_vertex_view,
                                                                         const VertexBvhResources& body_vertex_bvh,
                                                                         const ContactPairBuffers& contact_pairs,
                                                                         QOpenGLFunctions_4_5_Core& gl) const
{
#if CLOTH_SIM_COLLISION_GPU_TIMING
    const bool gpu_timing_started = cloth_face_body_vertex_timer_.begin(gl);
#endif
    gl.glUseProgram(cloth_face_body_vertex_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::cloth_current, motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::cloth_previous, motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::cloth_triangles, cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::body_vertex_ids, body_vertex_bvh.vertex_id_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::body_vertex_bvh, body_vertex_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::body_current, character_vertex_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::body_previous, character_vertex_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::pair_records, contact_pairs.pairs);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::pair_count, contact_pairs.count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_face_body_vertex_binding::overflow_count, contact_pairs.overflow_count);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program, cloth_face_body_vertex_.triangle_count, cloth_topology.triangle_count);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program, cloth_face_body_vertex_.max_pairs, contact_pairs.capacity);
    gl.glProgramUniform1f(cloth_face_body_vertex_.program, cloth_face_body_vertex_.thickness, collision_thickness_);
    gl.glProgramUniform1ui(cloth_face_body_vertex_.program, cloth_face_body_vertex_.ignored_body_part_mask, ignored_body_part_mask_);
    gl.glDispatchCompute(compute_group_count(cloth_topology.triangle_count, contact_generate_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#if CLOTH_SIM_COLLISION_GPU_TIMING
    if (gpu_timing_started) {
        cloth_face_body_vertex_timer_.end(gl);
    }
#endif
}

void ClothBodyContactGenerator::build_dispatch_command(const ContactPairBuffers& contact_pairs, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_build_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_build_binding::pair_count, contact_pairs.count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, dispatch_build_binding::dispatch_command, contact_pairs.dispatch_command);
    gl.glProgramUniform1ui(dispatch_build_.program, dispatch_build_.max_pairs, contact_pairs.capacity);
    gl.glProgramUniform1ui(dispatch_build_.program, dispatch_build_.local_size, contact_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

bool ClothBodyContactGenerator::has_programs() const
{
    return cloth_vertex_body_face_.program != 0 &&
           cloth_edge_body_edge_.program != 0 &&
           cloth_face_body_vertex_.program != 0 &&
           dispatch_build_.program != 0;
}
