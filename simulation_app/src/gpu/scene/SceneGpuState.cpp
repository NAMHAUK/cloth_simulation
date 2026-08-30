#include "gpu/scene/SceneGpuState.h"

#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <glm/vec4.hpp>

namespace {
constexpr std::uint32_t attachment_target_local_size = 128;
constexpr std::uint32_t normal_update_local_size = 128;
constexpr std::uint32_t dispatch_component_count = 3;
constexpr std::uint32_t candidate_capacity_multiplier = 8;

void create_buffer(GLuint& buffer, GLsizeiptr size, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glCreateBuffers(1, &buffer);
    gl.glNamedBufferData(buffer, size, nullptr, GL_DYNAMIC_DRAW);
}

void create_collision_candidate_buffer(CollisionCandidateBuffers& buffers,
                                       std::uint32_t element_count,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    buffers.max_pairs = element_count * candidate_capacity_multiplier;
    create_buffer(buffers.candidate_buffer, byte_size<glm::uvec2>(buffers.max_pairs), gl);
    create_buffer(buffers.count_buffer, byte_size<std::uint32_t>(1u), gl);
    create_buffer(buffers.dispatch_size_buffer, byte_size<std::uint32_t>(dispatch_component_count), gl);
}

void delete_collision_candidate_buffer(CollisionCandidateBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers.candidate_buffer);
    gl.glDeleteBuffers(1, &buffers.count_buffer);
    gl.glDeleteBuffers(1, &buffers.dispatch_size_buffer);
    buffers = {};
}
}

// Initialization

void SceneGpuState::initialize(const std::filesystem::path& shader_dir,
                               float attachment_surface_offset,
                               float body_detection_distance,
                               const SceneState& scene,
                               QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.initialize(gl);
    bvh_bounds_updater_.initialize(shader_dir, body_detection_distance, gl);
    character_gpu_state_.initialize(shader_dir, gl);
    buffer_bindings_.bind_character(character_gpu_state_.buffer_set(), gl);

    initialize_normal_programs(shader_dir, gl);
    initialize_attachment_target_program(shader_dir, attachment_surface_offset, gl);
    initialize_character_resources(scene, gl);
    set_character_motion(scene, gl);

    initialized_ = true;
}

void SceneGpuState::initialize_normal_programs(const std::filesystem::path& shader_dir,
                                               QOpenGLFunctions_4_5_Core& gl)
{
    const auto triangle_shader_path = shader_dir / "mesh" / "triangle_normal.comp";
    const auto vertex_shader_path = shader_dir / "mesh" / "vertex_normal.comp";
    triangle_normal_program_ = load_compute_program(triangle_shader_path, gl);
    vertex_normal_program_ = load_compute_program(vertex_shader_path, gl);

    triangle_count_location_ = require_uniform_location(triangle_normal_program_, "uTriangleCount", gl);
    vertex_count_location_ = require_uniform_location(vertex_normal_program_, "uVertexCount", gl);
    use_character_buffers_location_ =
        require_uniform_location(vertex_normal_program_, "uUseCharacterBuffers", gl);
}

void SceneGpuState::initialize_attachment_target_program(const std::filesystem::path& shader_dir,
                                                         float surface_offset,
                                                         QOpenGLFunctions_4_5_Core& gl)
{
    const auto shader_path = shader_dir / "cloth/setup/attachment_target_build.comp";
    attachment_target_program_ = load_compute_program(shader_path, gl);

    const GLuint program = attachment_target_program_;
    attachment_constraint_offset_location_ = require_uniform_location(program, "uConstraintOffset", gl);
    attachment_constraint_count_location_ = require_uniform_location(program, "uConstraintCount", gl);
    const GLint surface_offset_location = require_uniform_location(program, "uSurfaceOffset", gl);

    gl.glProgramUniform1f(program, surface_offset_location, surface_offset);
}

void SceneGpuState::initialize_character_resources(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    const Bvh& triangle_bvh = scene.default_body_triangle_bvh();
    const Bvh& vertex_bvh = scene.default_body_vertex_bvh();
    const Bvh& edge_bvh = scene.default_body_edge_bvh();

    character_gpu_state_.initialize_mesh(scene.character_motion(), triangle_bvh, vertex_bvh, edge_bvh, gl);
    bvh_bounds_updater_.set_body_level_offsets(triangle_bvh.level_offsets,
                                               vertex_bvh.level_offsets,
                                               edge_bvh.level_offsets);
}

// Character

void SceneGpuState::set_character_motion(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.set_motion(scene.character_motion(), gl);
    bvh_bounds_updater_.update_body_bvh(simulation_view(), gl);
    update_character_vertex_normals(gl);
}

void SceneGpuState::update_character_pose(const SceneState& scene,
                                          float frame_alpha,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.update_pose(scene.motion_frame_index(), frame_alpha, gl);
    bvh_bounds_updater_.update_body_bvh(simulation_view(), gl);
    update_character_vertex_normals(gl);
}

// Garments

void SceneGpuState::rebuild_garment_resources(const SceneState& scene,
                                              QOpenGLFunctions_4_5_Core& gl,
                                              GarmentLayer changed_layer)
{
    assert(!scene.garments().empty());

    cloth_gpu_state_.rebuild_buffers(scene.garments(), changed_layer, gl);
    buffer_bindings_.bind_cloth(cloth_gpu_state_.buffer_set(), gl);
    update_cloth_normals(gl);
}

void SceneGpuState::rebuild_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    release_collision_buffers(gl);

    const auto views = simulation_view();
    const auto& topology = views.cloth_topology;
    const auto& stretch_constraints = views.stretch_constraints;

    create_collision_candidate_buffer(collision_buffers_.cloth_vertex_body_face, topology.vertex_count, gl);
    create_collision_candidate_buffer(collision_buffers_.cloth_edge_body_edge,
                                      stretch_constraints.constraint_count,
                                      gl);
    create_collision_candidate_buffer(collision_buffers_.cloth_face_body_vertex, topology.triangle_count, gl);
    if (views.has_multiple_garments()) {
        create_collision_candidate_buffer(collision_buffers_.cloth_cloth_vertex_face,
                                          topology.vertex_count,
                                          gl);
    }

    const GLsizeiptr correction_bytes = byte_size<glm::ivec4>(topology.vertex_count);
    create_buffer(collision_buffers_.normal_correction_sum_buffer, correction_bytes, gl);
    create_buffer(collision_buffers_.friction_correction_sum_buffer, correction_bytes, gl);
    create_buffer(collision_buffers_.contact_motion_delta_sum_buffer, correction_bytes, gl);
    buffer_bindings_.bind_collision(collision_buffers_, gl);
}

void SceneGpuState::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.upload_garment_placement(garment, gl);
}

void SceneGpuState::initialize_garment_attachments(const GarmentObject& garment,
                                                   QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.upload_attachment_indices(garment, gl);
    build_attachment_targets(garment.layer, gl);
    cloth_gpu_state_.activate_attachment_targets(garment.layer);
}

void SceneGpuState::build_attachment_targets(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl)
{
    const SimulationGpuView views = simulation_view();
    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
    const std::uint32_t constraint_count = garment_state.attachment_constraint_count;
    if (constraint_count == 0u) {
        return;
    }

    gl.glUseProgram(attachment_target_program_);
    gl.glProgramUniform1ui(attachment_target_program_,
                           attachment_constraint_offset_location_,
                           garment_state.attachment_constraint_start_index);
    gl.glProgramUniform1ui(attachment_target_program_,
                           attachment_constraint_count_location_,
                           constraint_count);

    gl.glDispatchCompute(compute_group_count(constraint_count, attachment_target_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void SceneGpuState::capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized()) {
        return;
    }
    cloth_gpu_state_.capture_base_positions(gl);
}

void SceneGpuState::restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (!cloth_gpu_state_.is_initialized()) {
        return;
    }
    cloth_gpu_state_.restore_base_positions(gl);
    update_cloth_normals(gl);
}

void SceneGpuState::clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    cloth_gpu_state_.clear_base_positions(gl);
}

void SceneGpuState::update_cloth_bvh_bounds(const SimulationGpuView& views,
                                            float bounds_margin,
                                            QOpenGLFunctions_4_5_Core& gl)
{
    if (views.has_multiple_garments()) {
        bvh_bounds_updater_.update_cloth_bvh(views, bounds_margin, gl);
    }
}

// Normals

void SceneGpuState::update_cloth_normals(QOpenGLFunctions_4_5_Core& gl)
{
    const ClothMeshTopologyResources topology = cloth_gpu_state_.mesh_topology_resources();

    // triangle normal update
    gl.glUseProgram(triangle_normal_program_);
    gl.glProgramUniform1ui(triangle_normal_program_, triangle_count_location_, topology.triangle_count);
    gl.glDispatchCompute(compute_group_count(topology.triangle_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // vertex normal update
    gl.glUseProgram(vertex_normal_program_);
    gl.glProgramUniform1ui(vertex_normal_program_, vertex_count_location_, topology.vertex_count);
    gl.glProgramUniform1i(vertex_normal_program_, use_character_buffers_location_, GL_FALSE);
    gl.glDispatchCompute(compute_group_count(topology.vertex_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void SceneGpuState::update_character_vertex_normals(QOpenGLFunctions_4_5_Core& gl)
{
    const CharacterMeshTopologyResources topology = character_gpu_state_.mesh_topology_resources();

    gl.glUseProgram(vertex_normal_program_);
    gl.glProgramUniform1ui(vertex_normal_program_, vertex_count_location_, topology.vertex_count);
    gl.glProgramUniform1i(vertex_normal_program_, use_character_buffers_location_, GL_TRUE);
    gl.glDispatchCompute(compute_group_count(topology.vertex_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

// Accessors

bool SceneGpuState::is_initialized() const
{
    return initialized_;
}

SimulationGpuView SceneGpuState::simulation_view() const
{
    SimulationGpuView views(cloth_gpu_state_.garment_buffer_states());
    views.cloth_motion = cloth_gpu_state_.motion_buffer_view();
    views.cloth_collision_pushout = cloth_gpu_state_.collision_pushout_buffer_view();
    views.cloth_contact_motion = cloth_gpu_state_.contact_motion_buffer_view();
    views.cloth_body_triangle_indices = cloth_gpu_state_.body_triangle_index_buffer_view();
    views.cloth_topology = cloth_gpu_state_.mesh_topology_resources();
    views.cloth_bvh = cloth_gpu_state_.cloth_bvh_buffer_view();
    views.stretch_constraints = cloth_gpu_state_.stretch_constraint_buffer_view();
    views.bending_constraints = cloth_gpu_state_.bending_constraint_buffer_view();
    views.attachment_constraints = cloth_gpu_state_.attachment_constraint_buffer_view();
    views.body_topology = character_gpu_state_.mesh_topology_resources();
    views.body_vertices = character_gpu_state_.vertex_buffer_view();
    views.body_triangles = character_gpu_state_.body_triangle_resources();
    views.body_triangle_bvh = character_gpu_state_.body_triangle_bvh_buffer_view();
    views.body_vertex_bvh = character_gpu_state_.body_vertex_bvh_buffer_view();
    views.body_edge_bvh = character_gpu_state_.body_edge_bvh_buffer_view();
    views.collision = collision_buffers_;
    return views;
}

const CharacterGpuState& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

const ClothGpuState& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

// Release

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_garment_resources(gl);
    buffer_bindings_.restore_character(gl);
    character_gpu_state_.release(gl);
    bvh_bounds_updater_.release(gl);
    gl.glDeleteProgram(vertex_normal_program_);
    gl.glDeleteProgram(triangle_normal_program_);
    gl.glDeleteProgram(attachment_target_program_);
    buffer_bindings_.release(gl);

    triangle_normal_program_ = 0;
    vertex_normal_program_ = 0;
    triangle_count_location_ = -1;
    vertex_count_location_ = -1;
    use_character_buffers_location_ = -1;
    attachment_target_program_ = 0;
    initialized_ = false;
}

void SceneGpuState::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    release_collision_buffers(gl);
    buffer_bindings_.restore_cloth(gl);
    cloth_gpu_state_.release(gl);
}

void SceneGpuState::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.restore_collision(gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_vertex_body_face, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_edge_body_edge, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_face_body_vertex, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_cloth_vertex_face, gl);
    gl.glDeleteBuffers(1, &collision_buffers_.normal_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.friction_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.contact_motion_delta_sum_buffer);
    collision_buffers_ = {};
}
