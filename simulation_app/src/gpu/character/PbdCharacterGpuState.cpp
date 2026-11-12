#include "gpu/character/PbdCharacterGpuState.h"
#include "gpu/character/CharacterGpuState.h"
#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"
#include "utils/MeshGeometryUtils.h"
#include "utils/ShaderUtils.h"

#include <cstddef>

#include <glm/vec4.hpp>

namespace {
constexpr std::uint32_t position_update_local_size = 128;
constexpr std::uint32_t triangle_geometry_local_size = 128;
constexpr std::size_t triangle_vertex_count = 3u;
}

// Initialization

void PbdCharacterGpuState::initialize(const std::filesystem::path& shader_dir,
                                      const SceneState& scene,
                                      const CharacterGpuState& character_state,
                                      QOpenGLFunctions_4_5_Core& gl)
{
    // load shader program
    const auto position_shader_path = shader_dir / "character" / "vertex_position_update.comp";
    const auto triangle_shader_path = shader_dir / "character" / "triangle_geometry_update.comp";
    position_program_ = load_compute_program(position_shader_path, gl);
    triangle_update_program_ = load_compute_program(triangle_shader_path, gl);

    position_current_frame_base_loc_ = require_uniform_location(position_program_, "uCurrentFrameBase", gl);
    position_next_frame_base_loc_ = require_uniform_location(position_program_, "uNextFrameBase", gl);
    position_frame_alpha_loc_ = require_uniform_location(position_program_, "uFrameAlpha", gl);
    position_vertex_count_loc_ = require_uniform_location(position_program_, "uVertexCount", gl);
    triangle_count_loc_ = require_uniform_location(triangle_update_program_, "uTriangleCount", gl);

    // create PBD buffers
    const TriangleBvh& body_triangle_bvh = scene.default_body_triangle_bvh();
    const EdgeBvh& body_edge_bvh = scene.default_body_edge_bvh();
    const auto adjacency = build_vertex_triangle_adjacency(character_state.vertex_count(),
                                                           body_triangle_bvh.triangle_vertex_indices);

    gl.glCreateBuffers(1, &buffers_.animation_positions);

    buffers_.previous_position = create_buffer(byte_size<glm::vec4>(character_state.vertex_count()),
                                               nullptr,
                                               GL_DYNAMIC_DRAW,
                                               gl);
    copy_current_to_previous(character_state, gl);

    buffers_.body_triangle_bvh_node = create_buffer(byte_size<BvhNode>(body_triangle_bvh.nodes.size()),
                                                    body_triangle_bvh.nodes.data(),
                                                    GL_DYNAMIC_DRAW,
                                                    gl);
    buffers_.body_triangle_bounds = create_buffer(byte_size<Aabb>(body_triangle_bvh.leaf_element_count()),
                                                  nullptr,
                                                  GL_DYNAMIC_DRAW,
                                                  gl);
    buffers_.body_edge_bvh_node = create_buffer(byte_size<BvhNode>(body_edge_bvh.nodes.size()),
                                                body_edge_bvh.nodes.data(),
                                                GL_DYNAMIC_DRAW,
                                                gl);
    buffers_.body_edge_vertex_indices = create_buffer(byte_size<MeshEdge>(body_edge_bvh.edge_vertex_indices.size()),
                                                      body_edge_bvh.edge_vertex_indices.data(),
                                                      GL_STATIC_DRAW,
                                                      gl);
    buffers_.body_edge_bounds = create_buffer(byte_size<Aabb>(body_edge_bvh.edge_vertex_indices.size()),
                                              nullptr,
                                              GL_DYNAMIC_DRAW,
                                              gl);
    buffers_.adjacent_triangle_offsets = create_buffer(byte_size<std::uint32_t>(adjacency.offsets.size()),
                                                       adjacency.offsets.data(),
                                                       GL_STATIC_DRAW,
                                                       gl);
    buffers_.adjacent_triangle_indices = create_buffer(byte_size<std::uint32_t>(adjacency.triangle_indices.size()),
                                                       adjacency.triangle_indices.data(),
                                                       GL_STATIC_DRAW,
                                                       gl);
    buffers_.triangle_position = create_buffer(byte_size<glm::vec4>(adjacency.triangle_count * triangle_vertex_count),
                                               nullptr,
                                               GL_DYNAMIC_DRAW,
                                               gl);
    buffers_.triangle_normal = create_buffer(byte_size<glm::vec4>(adjacency.triangle_count),
                                             nullptr,
                                             GL_DYNAMIC_DRAW,
                                             gl);
}

// Motion update

void PbdCharacterGpuState::set_motion(const CharacterMotion& motion,
                                      const CharacterGpuState& character_state,
                                      QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferData(buffers_.animation_positions,
                         byte_size<glm::vec3>(motion.vertex_positions.size()),
                         motion.vertex_positions.data(),
                         GL_STATIC_DRAW);

    frame_count_ = motion.frame_count;
    current_frame_index_ = 0;

    write_current_positions(0.0f, character_state, gl);
    copy_current_to_previous(character_state, gl);
    update_triangle_geometry(character_state, gl);
}

void PbdCharacterGpuState::update_pose(std::uint32_t frame_index,
                                       float frame_alpha,
                                       const CharacterGpuState& character_state,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    current_frame_index_ = frame_index;

    copy_current_to_previous(character_state, gl);
    write_current_positions(frame_alpha, character_state, gl);
    update_triangle_geometry(character_state, gl);
}

void PbdCharacterGpuState::write_current_positions(float frame_alpha,
                                                   const CharacterGpuState& character_state,
                                                   QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t next_frame_index = current_frame_index_ + 1u < frame_count_ ? current_frame_index_ + 1u
                                                                                    : current_frame_index_;
    const std::uint32_t current_frame_base = current_frame_index_ * character_state.vertex_count();
    const std::uint32_t next_frame_base = next_frame_index * character_state.vertex_count();

    gl.glUseProgram(position_program_);
    gl.glProgramUniform1ui(position_program_, position_current_frame_base_loc_, current_frame_base);
    gl.glProgramUniform1ui(position_program_, position_next_frame_base_loc_, next_frame_base);
    gl.glProgramUniform1f(position_program_, position_frame_alpha_loc_, frame_alpha);
    gl.glProgramUniform1ui(position_program_, position_vertex_count_loc_, character_state.vertex_count());

    gl.glDispatchCompute(compute_group_count(character_state.vertex_count(), position_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void PbdCharacterGpuState::copy_current_to_previous(const CharacterGpuState& character_state,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    const auto position_bytes = byte_size<glm::vec4>(character_state.vertex_count());
    gl.glCopyNamedBufferSubData(character_state.buffer_set().current_position,
                                buffers_.previous_position,
                                0,
                                0,
                                position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void PbdCharacterGpuState::update_triangle_geometry(const CharacterGpuState& character_state,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(triangle_update_program_);
    gl.glProgramUniform1ui(triangle_update_program_, triangle_count_loc_, character_state.triangle_count());

    gl.glDispatchCompute(compute_group_count(character_state.triangle_count(), triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Accessors

const PbdCharacterBufferSet& PbdCharacterGpuState::buffer_set() const
{
    return buffers_;
}

// Release

void PbdCharacterGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.triangle_normal);
    gl.glDeleteBuffers(1, &buffers_.triangle_position);
    gl.glDeleteBuffers(1, &buffers_.adjacent_triangle_indices);
    gl.glDeleteBuffers(1, &buffers_.adjacent_triangle_offsets);
    gl.glDeleteBuffers(1, &buffers_.body_triangle_bvh_node);
    gl.glDeleteBuffers(1, &buffers_.body_edge_bounds);
    gl.glDeleteBuffers(1, &buffers_.body_edge_vertex_indices);
    gl.glDeleteBuffers(1, &buffers_.body_edge_bvh_node);
    gl.glDeleteBuffers(1, &buffers_.body_triangle_bounds);
    gl.glDeleteBuffers(1, &buffers_.previous_position);
    gl.glDeleteBuffers(1, &buffers_.animation_positions);

    gl.glDeleteProgram(triangle_update_program_);
    gl.glDeleteProgram(position_program_);

    position_program_ = 0;
    triangle_update_program_ = 0;
    position_current_frame_base_loc_ = -1;
    position_next_frame_base_loc_ = -1;
    position_frame_alpha_loc_ = -1;
    position_vertex_count_loc_ = -1;
    triangle_count_loc_ = -1;

    buffers_ = {};
    frame_count_ = 0;
    current_frame_index_ = 0;
}
