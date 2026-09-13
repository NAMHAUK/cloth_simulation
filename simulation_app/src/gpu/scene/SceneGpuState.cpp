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

    triangle_count_loc_ = require_uniform_location(triangle_normal_program_, "uTriangleCount", gl);
    vertex_count_loc_ = require_uniform_location(vertex_normal_program_, "uVertexCount", gl);
    use_character_buffers_loc_ = require_uniform_location(vertex_normal_program_, "uUseCharacterBuffers", gl);
}

void SceneGpuState::initialize_attachment_target_program(const std::filesystem::path& shader_dir,
                                                         float surface_offset,
                                                         QOpenGLFunctions_4_5_Core& gl)
{
    const auto shader_path = shader_dir / "cloth/setup/attachment_target_build.comp";
    attachment_target_program_ = load_compute_program(shader_path, gl);

    const GLuint program = attachment_target_program_;
    attachment_constraint_offset_loc_ = require_uniform_location(program, "uConstraintOffset", gl);
    attachment_constraint_count_loc_ = require_uniform_location(program, "uConstraintCount", gl);
    const GLint surface_offset_loc = require_uniform_location(program, "uSurfaceOffset", gl);

    gl.glProgramUniform1f(program, surface_offset_loc, surface_offset);
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
    bvh_bounds_updater_.update_body_bvh(gl);
    update_character_vertex_normals(gl);
}

void SceneGpuState::update_character_pose(const SceneState& scene,
                                          float frame_alpha,
                                          QOpenGLFunctions_4_5_Core& gl,
                                          const GLuint* body_bounds_queries)
{
    character_gpu_state_.update_pose(scene.motion_frame_index(), frame_alpha, gl);
    if (body_bounds_queries) {
        gl.glQueryCounter(body_bounds_queries[0], GL_TIMESTAMP);
    }
    bvh_bounds_updater_.update_body_bvh(gl);
    if (body_bounds_queries) {
        gl.glQueryCounter(body_bounds_queries[1], GL_TIMESTAMP);
    }
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

    const ClothBufferElementCounts& counts = cloth_gpu_state_.element_counts();

    create_collision_candidate_buffer(collision_buffers_.cloth_vertex_body_face, counts.vertex, gl);
    create_collision_candidate_buffer(collision_buffers_.cloth_edge_body_edge, counts.stretch_constraint, gl);
    create_collision_candidate_buffer(collision_buffers_.cloth_face_body_vertex, counts.triangle, gl);
    const bool cloth_cloth_active = cloth_gpu_state_.has_multiple_garments();
    if (cloth_cloth_active) {
        create_collision_candidate_buffer(collision_buffers_.cloth_cloth_vertex_face, counts.vertex, gl);
    }

    const GLsizeiptr correction_bytes = byte_size<glm::ivec4>(counts.vertex);
    create_buffer(collision_buffers_.normal_correction_sum_buffer, correction_bytes, gl);
    create_buffer(collision_buffers_.friction_correction_sum_buffer, correction_bytes, gl);
    create_buffer(collision_buffers_.contact_motion_delta_sum_buffer, correction_bytes, gl);
    buffer_bindings_.bind_collision(collision_buffers_, cloth_cloth_active, gl);
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
    const GarmentBufferState& garment_state = cloth_gpu_state_.garment_states()[layer];
    const std::uint32_t constraint_count = garment_state.attachment_constraint_count;
    if (constraint_count == 0u) {
        return;
    }

    gl.glUseProgram(attachment_target_program_);
    gl.glProgramUniform1ui(attachment_target_program_,
                           attachment_constraint_offset_loc_,
                           garment_state.attachment_constraint_start_index);
    gl.glProgramUniform1ui(attachment_target_program_, attachment_constraint_count_loc_, constraint_count);

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

void SceneGpuState::update_cloth_bvh_bounds(float bounds_margin, QOpenGLFunctions_4_5_Core& gl)
{
    if (cloth_gpu_state_.has_multiple_garments()) {
        bvh_bounds_updater_.update_cloth_bvh(cloth_gpu_state_, bounds_margin, gl);
    }
}

// Normals

void SceneGpuState::update_cloth_normals(QOpenGLFunctions_4_5_Core& gl)
{
    const ClothBufferElementCounts& counts = cloth_gpu_state_.element_counts();

    // triangle normal update
    gl.glUseProgram(triangle_normal_program_);
    gl.glProgramUniform1ui(triangle_normal_program_, triangle_count_loc_, counts.triangle);
    gl.glDispatchCompute(compute_group_count(counts.triangle, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // vertex normal update
    gl.glUseProgram(vertex_normal_program_);
    gl.glProgramUniform1ui(vertex_normal_program_, vertex_count_loc_, counts.vertex);
    gl.glProgramUniform1i(vertex_normal_program_, use_character_buffers_loc_, GL_FALSE);
    gl.glDispatchCompute(compute_group_count(counts.vertex, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}

void SceneGpuState::update_character_vertex_normals(QOpenGLFunctions_4_5_Core& gl)
{
    const std::uint32_t vertex_count = character_gpu_state_.vertex_count();

    gl.glUseProgram(vertex_normal_program_);
    gl.glProgramUniform1ui(vertex_normal_program_, vertex_count_loc_, vertex_count);
    gl.glProgramUniform1i(vertex_normal_program_, use_character_buffers_loc_, GL_TRUE);
    gl.glDispatchCompute(compute_group_count(vertex_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Accessors

bool SceneGpuState::is_initialized() const
{
    return initialized_;
}

const CharacterGpuState& SceneGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

const ClothGpuState& SceneGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

const CollisionBuffers& SceneGpuState::collision_buffers() const
{
    return collision_buffers_;
}

// Release

void SceneGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_garment_resources(gl);
    buffer_bindings_.reset_character_bindings(gl);
    character_gpu_state_.release(gl);
    bvh_bounds_updater_.release(gl);
    gl.glDeleteProgram(vertex_normal_program_);
    gl.glDeleteProgram(triangle_normal_program_);
    gl.glDeleteProgram(attachment_target_program_);
    buffer_bindings_.release(gl);

    triangle_normal_program_ = 0;
    vertex_normal_program_ = 0;
    triangle_count_loc_ = -1;
    vertex_count_loc_ = -1;
    use_character_buffers_loc_ = -1;
    attachment_target_program_ = 0;
    initialized_ = false;
}

void SceneGpuState::release_garment_resources(QOpenGLFunctions_4_5_Core& gl)
{
    release_collision_buffers(gl);
    buffer_bindings_.reset_cloth_bindings(gl);
    cloth_gpu_state_.release(gl);
}

void SceneGpuState::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    buffer_bindings_.reset_collision_bindings(gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_vertex_body_face, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_edge_body_edge, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_face_body_vertex, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_cloth_vertex_face, gl);
    gl.glDeleteBuffers(1, &collision_buffers_.normal_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.friction_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.contact_motion_delta_sum_buffer);
    collision_buffers_ = {};
}
