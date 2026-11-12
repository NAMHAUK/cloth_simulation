#include "gpu/pbd/PbdGpuState.h"
#include "gpu/common/ClothGpuState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr std::uint32_t attachment_target_local_size = 128;
constexpr std::uint32_t normal_update_local_size = 128;
constexpr GLsizeiptr dispatch_size_bytes = byte_size<std::uint32_t>(3u);
constexpr std::uint32_t candidate_capacity_multiplier = 8;

void delete_collision_candidate_buffer(CollisionCandidateBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers.candidate_buffer);
    gl.glDeleteBuffers(1, &buffers.count_buffer);
    gl.glDeleteBuffers(1, &buffers.dispatch_size_buffer);
    buffers = {};
}

}

// Initialization

void PbdGpuState::initialize(const std::filesystem::path& shader_dir,
                             float attachment_surface_offset,
                             float body_detection_distance,
                             const SceneState& scene,
                             const CharacterGpuState& character_state,
                             QOpenGLFunctions_4_5_Core& gl)
{
    character_gpu_state_.initialize(shader_dir, scene, character_state, gl);
    bvh_bounds_updater_.initialize(shader_dir, body_detection_distance, scene, gl);

    // load normal update shaders
    const auto triangle_shader_path = shader_dir / "mesh" / "triangle_normal.comp";
    const auto vertex_shader_path = shader_dir / "mesh" / "vertex_normal.comp";
    triangle_normal_program_ = load_compute_program(triangle_shader_path, gl);
    vertex_normal_program_ = load_compute_program(vertex_shader_path, gl);

    triangle_count_loc_ = require_uniform_location(triangle_normal_program_, "uTriangleCount", gl);
    vertex_count_loc_ = require_uniform_location(vertex_normal_program_, "uVertexCount", gl);
    use_character_buffers_loc_ = require_uniform_location(vertex_normal_program_, "uUseCharacterBuffers", gl);

    // load attachment target shader
    const auto shader_path = shader_dir / "cloth/setup/attachment_target_build.comp";
    attachment_target_program_ = load_compute_program(shader_path, gl);

    const GLuint program = attachment_target_program_;
    attachment_constraint_offset_loc_ = require_uniform_location(program, "uConstraintOffset", gl);
    attachment_constraint_count_loc_ = require_uniform_location(program, "uConstraintCount", gl);
    const GLint surface_offset_loc = require_uniform_location(program, "uSurfaceOffset", gl);

    gl.glProgramUniform1f(program, surface_offset_loc, attachment_surface_offset);
}

// Collision buffers

void PbdGpuState::create_collision_buffers(std::uint32_t vertex_count, QOpenGLFunctions_4_5_Core& gl)
{
    // Cloth vertex / body face
    {
        auto& buffers = collision_buffers_.cloth_vertex_body_face;
        buffers.max_pairs = vertex_count * candidate_capacity_multiplier;
        buffers.candidate_buffer = create_buffer(byte_size<glm::uvec2>(buffers.max_pairs),
                                                 nullptr,
                                                 GL_DYNAMIC_DRAW,
                                                 gl);
        buffers.count_buffer = create_buffer(byte_size<std::uint32_t>(1u), nullptr, GL_DYNAMIC_DRAW, gl);
        buffers.dispatch_size_buffer = create_buffer(dispatch_size_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    }

    // Cloth edge / body edge
    {
        auto& buffers = collision_buffers_.cloth_edge_body_edge;
        buffers.max_pairs = cloth_gpu_state_.stretch_constraint_count() * candidate_capacity_multiplier;
        buffers.candidate_buffer = create_buffer(byte_size<glm::uvec2>(buffers.max_pairs),
                                                 nullptr,
                                                 GL_DYNAMIC_DRAW,
                                                 gl);
        buffers.count_buffer = create_buffer(byte_size<std::uint32_t>(1u), nullptr, GL_DYNAMIC_DRAW, gl);
        buffers.dispatch_size_buffer = create_buffer(dispatch_size_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    }

    // Cloth vertex / cloth face
    {
        auto& buffers = collision_buffers_.cloth_cloth_vertex_face;
        buffers.max_pairs = vertex_count * candidate_capacity_multiplier * 4;
        buffers.candidate_buffer = create_buffer(byte_size<glm::uvec2>(buffers.max_pairs),
                                                 nullptr,
                                                 GL_DYNAMIC_DRAW,
                                                 gl);
        buffers.count_buffer = create_buffer(byte_size<std::uint32_t>(1u), nullptr, GL_DYNAMIC_DRAW, gl);
        buffers.dispatch_size_buffer = create_buffer(dispatch_size_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    }

    // Cloth edge / cloth edge
    {
        auto& buffers = collision_buffers_.cloth_cloth_edge_edge;
        buffers.max_pairs = cloth_gpu_state_.edge_count() * candidate_capacity_multiplier * 4u;
        buffers.candidate_buffer = create_buffer(byte_size<glm::uvec2>(buffers.max_pairs),
                                                 nullptr,
                                                 GL_DYNAMIC_DRAW,
                                                 gl);
        buffers.count_buffer = create_buffer(byte_size<std::uint32_t>(1u), nullptr, GL_DYNAMIC_DRAW, gl);
        buffers.dispatch_size_buffer = create_buffer(dispatch_size_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    }

    const GLsizeiptr correction_bytes = byte_size<glm::ivec4>(vertex_count);
    collision_buffers_.normal_correction_sum_buffer = create_buffer(correction_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    collision_buffers_.friction_correction_sum_buffer = create_buffer(correction_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    collision_buffers_.inward_motion_correction_sum_buffer = create_buffer(correction_bytes,
                                                                           nullptr,
                                                                           GL_DYNAMIC_DRAW,
                                                                           gl);
}

void PbdGpuState::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    delete_collision_candidate_buffer(collision_buffers_.cloth_vertex_body_face, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_edge_body_edge, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_cloth_vertex_face, gl);
    delete_collision_candidate_buffer(collision_buffers_.cloth_cloth_edge_edge, gl);
    gl.glDeleteBuffers(1, &collision_buffers_.normal_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.friction_correction_sum_buffer);
    gl.glDeleteBuffers(1, &collision_buffers_.inward_motion_correction_sum_buffer);
    collision_buffers_ = {};
}

// BVH bounds

void PbdGpuState::update_body_bvh_bounds(QOpenGLFunctions_4_5_Core& gl) const
{
    bvh_bounds_updater_.update_body_bvh(gl);
}

void PbdGpuState::update_cloth_bvh_bounds(float bounds_margin, QOpenGLFunctions_4_5_Core& gl) const
{
    bvh_bounds_updater_.update_cloth_bvh(cloth_gpu_state_, bounds_margin, gl);
}

// Attachments

void PbdGpuState::build_attachment_targets(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const
{
    const AttachmentBufferState& attachment_state = cloth_gpu_state_.attachment_states()[layer];
    const std::uint32_t constraint_count = attachment_state.count;
    if (constraint_count == 0u) {
        return;
    }

    gl.glUseProgram(attachment_target_program_);
    gl.glProgramUniform1ui(attachment_target_program_, attachment_constraint_offset_loc_, attachment_state.start_index);
    gl.glProgramUniform1ui(attachment_target_program_, attachment_constraint_count_loc_, constraint_count);

    gl.glDispatchCompute(compute_group_count(constraint_count, attachment_target_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Normals

void PbdGpuState::update_cloth_normals(const ClothBufferElementCounts& counts, QOpenGLFunctions_4_5_Core& gl) const
{
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

void PbdGpuState::update_character_vertex_normals(std::uint32_t vertex_count, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(vertex_normal_program_);
    gl.glProgramUniform1ui(vertex_normal_program_, vertex_count_loc_, vertex_count);
    gl.glProgramUniform1i(vertex_normal_program_, use_character_buffers_loc_, GL_TRUE);
    gl.glDispatchCompute(compute_group_count(vertex_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Accessors

PbdCharacterGpuState& PbdGpuState::character_gpu_state()
{
    return character_gpu_state_;
}

const PbdCharacterGpuState& PbdGpuState::character_gpu_state() const
{
    return character_gpu_state_;
}

PbdClothGpuState& PbdGpuState::cloth_gpu_state()
{
    return cloth_gpu_state_;
}

const PbdClothGpuState& PbdGpuState::cloth_gpu_state() const
{
    return cloth_gpu_state_;
}

const CollisionBuffers& PbdGpuState::collision_buffers() const
{
    return collision_buffers_;
}

// Release

void PbdGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_collision_buffers(gl);
    cloth_gpu_state_.release(gl);
    character_gpu_state_.release(gl);
    bvh_bounds_updater_.release(gl);
    gl.glDeleteProgram(vertex_normal_program_);
    gl.glDeleteProgram(triangle_normal_program_);
    gl.glDeleteProgram(attachment_target_program_);

    triangle_normal_program_ = 0;
    vertex_normal_program_ = 0;
    triangle_count_loc_ = -1;
    vertex_count_loc_ = -1;
    use_character_buffers_loc_ = -1;
    attachment_target_program_ = 0;
}
