#include "gpu/character/CharacterGpuState.h"

#include "asset/MeshGeometryUtils.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"
#include <cstddef>
#include <stdexcept>

#include <glm/vec4.hpp>

namespace {
constexpr GLuint all_frame_positions_binding = 0;
constexpr GLuint current_positions_binding = 1;
constexpr GLuint triangle_position_binding = 0;
constexpr GLuint triangle_indices_binding = 1;
constexpr GLuint triangle_positions_binding = 2;
constexpr GLuint triangle_normals_binding = 3;
constexpr std::uint32_t position_update_local_size = 128;
constexpr std::uint32_t triangle_geometry_local_size = 128;
constexpr std::size_t triangle_vertex_count = 3u;
}

// Initialization

void CharacterGpuState::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const auto position_shader_path = shader_dir / "character" / "vertex_position_update.comp";
    const auto triangle_shader_path = shader_dir / "character" / "triangle_geometry_update.comp";
    position_program_ = load_compute_program(position_shader_path, gl);
    triangle_update_program_ = load_compute_program(triangle_shader_path, gl);

    position_current_frame_base_location_ =
        require_uniform_location(position_program_, "uCurrentFrameBase", gl);
    position_next_frame_base_location_ = require_uniform_location(position_program_, "uNextFrameBase", gl);
    position_frame_alpha_location_ = require_uniform_location(position_program_, "uFrameAlpha", gl);
    position_vertex_count_location_ = require_uniform_location(position_program_, "uVertexCount", gl);
    triangle_count_location_ = require_uniform_location(triangle_update_program_, "uTriangleCount", gl);

    initialize_gpu_resources(gl);
}

void CharacterGpuState::initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl)
{
    // Create persistent OpenGL objects.
    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &buffers_.all_frame_positions);
    gl.glCreateBuffers(1, &buffers_.previous_position);
    gl.glCreateBuffers(1, &buffers_.current_position);
    gl.glCreateBuffers(1, &buffers_.triangle_index);
    gl.glCreateBuffers(1, &buffers_.body_triangle_bvh_node);
    gl.glCreateBuffers(1, &buffers_.body_triangle_bounds);
    gl.glCreateBuffers(1, &buffers_.body_vertex_bvh_node);
    gl.glCreateBuffers(1, &buffers_.body_vertex_bvh_vertex_index);
    gl.glCreateBuffers(1, &buffers_.body_vertex_bounds);
    gl.glCreateBuffers(1, &buffers_.body_edge_bvh_node);
    gl.glCreateBuffers(1, &buffers_.body_edge_index);
    gl.glCreateBuffers(1, &buffers_.body_edge_bounds);
    gl.glCreateBuffers(1, &buffers_.adjacent_triangle_offsets);
    gl.glCreateBuffers(1, &buffers_.adjacent_triangle_indices);
    gl.glCreateBuffers(1, &buffers_.triangle_position);
    gl.glCreateBuffers(1, &buffers_.triangle_normal);
    gl.glCreateBuffers(1, &buffers_.vertex_normal);

    gl.glVertexArrayElementBuffer(vao_, buffers_.triangle_index);
}

void CharacterGpuState::initialize_mesh(const CharacterMotion& motion,
                                        const Bvh& body_triangle_bvh,
                                        const Bvh& body_vertex_bvh,
                                        const Bvh& body_edge_bvh,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    const auto adjacency = build_vertex_triangle_adjacency(motion.vertex_count, body_triangle_bvh.indices);

    const auto position_bytes = byte_size<glm::vec4>(motion.vertex_count);

    gl.glNamedBufferData(buffers_.previous_position, position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.current_position, position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_index,
                         byte_size<std::uint32_t>(body_triangle_bvh.indices.size()),
                         body_triangle_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bvh_node,
                         byte_size<BvhNode>(body_triangle_bvh.nodes.size()),
                         body_triangle_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bounds,
                         byte_size<Aabb>(body_triangle_bvh.leaf_element_count()),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_node,
                         byte_size<BvhNode>(body_vertex_bvh.nodes.size()),
                         body_vertex_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_vertex_index,
                         byte_size<std::uint32_t>(body_vertex_bvh.indices.size()),
                         body_vertex_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bounds,
                         byte_size<Aabb>(motion.vertex_count),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bvh_node,
                         byte_size<BvhNode>(body_edge_bvh.nodes.size()),
                         body_edge_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_index,
                         byte_size<std::uint32_t>(body_edge_bvh.indices.size()),
                         body_edge_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bounds,
                         byte_size<Aabb>(body_edge_bvh.indices.size() / 2u),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_offsets,
                         byte_size<std::uint32_t>(adjacency.offsets.size()),
                         adjacency.offsets.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_indices,
                         byte_size<std::uint32_t>(adjacency.triangle_indices.size()),
                         adjacency.triangle_indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_position,
                         byte_size<glm::vec4>(adjacency.triangle_count * triangle_vertex_count),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_normal,
                         byte_size<glm::vec4>(adjacency.triangle_count),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.vertex_normal,
                         byte_size<glm::vec4>(motion.vertex_count),
                         nullptr,
                         GL_DYNAMIC_DRAW);

    vertex_count_ = motion.vertex_count;
    triangle_count_ = adjacency.triangle_count;
    arm_triangle_ranges_ = body_triangle_bvh.arm_triangle_ranges;
    index_count_ = static_cast<GLsizei>(body_triangle_bvh.indices.size());
}

// Motion update

void CharacterGpuState::set_motion(const CharacterMotion& motion, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferData(buffers_.all_frame_positions,
                         byte_size<float>(motion.vertices.size()),
                         motion.vertices.data(),
                         GL_STATIC_DRAW);

    frame_count_ = motion.frame_count;
    current_frame_index_ = 0;

    // Initialization writes the selected pose to current, then mirrors it into previous.
    write_current_positions(0.0f, gl);
    copy_current_to_previous(gl);
    update_triangle_geometry(gl);
}

void CharacterGpuState::update_pose(std::uint32_t frame_index,
                                    float frame_alpha,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    current_frame_index_ = frame_index;

    // Continuous update carries old current into previous before writing the new current pose.
    copy_current_to_previous(gl);
    write_current_positions(frame_alpha, gl);
    update_triangle_geometry(gl);
}

void CharacterGpuState::write_current_positions(float frame_alpha, QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t next_frame_index =
        current_frame_index_ + 1u < frame_count_ ? current_frame_index_ + 1u : current_frame_index_;
    const std::uint32_t current_frame_base = current_frame_index_ * vertex_count_ * 3u;
    const std::uint32_t next_frame_base = next_frame_index * vertex_count_ * 3u;

    gl.glUseProgram(position_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, all_frame_positions_binding, buffers_.all_frame_positions);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, current_positions_binding, buffers_.current_position);
    gl.glProgramUniform1ui(position_program_, position_current_frame_base_location_, current_frame_base);
    gl.glProgramUniform1ui(position_program_, position_next_frame_base_location_, next_frame_base);
    gl.glProgramUniform1f(position_program_, position_frame_alpha_location_, frame_alpha);
    gl.glProgramUniform1ui(position_program_, position_vertex_count_location_, vertex_count_);

    gl.glDispatchCompute(compute_group_count(vertex_count_, position_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CharacterGpuState::copy_current_to_previous(QOpenGLFunctions_4_5_Core& gl) const
{
    const auto position_bytes = byte_size<glm::vec4>(vertex_count_);
    gl.glCopyNamedBufferSubData(buffers_.current_position, buffers_.previous_position, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CharacterGpuState::update_triangle_geometry(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(triangle_update_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_position_binding, buffers_.current_position);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_indices_binding, buffers_.triangle_index);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_positions_binding, buffers_.triangle_position);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_normals_binding, buffers_.triangle_normal);
    gl.glProgramUniform1ui(triangle_update_program_, triangle_count_location_, triangle_count_);

    gl.glDispatchCompute(compute_group_count(triangle_count_, triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Rendering

void CharacterGpuState::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void CharacterGpuState::bind_current_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.current_position);
}

void CharacterGpuState::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

// Accessors

CharacterMeshTopologyResources CharacterGpuState::mesh_topology_resources() const
{
    CharacterMeshTopologyResources topology;
    topology.triangle_index_buffer = buffers_.triangle_index;
    topology.adjacent_triangle_offsets_buffer = buffers_.adjacent_triangle_offsets;
    topology.adjacent_triangle_indices_buffer = buffers_.adjacent_triangle_indices;
    topology.bvh_vertex_index_buffer = buffers_.body_vertex_bvh_vertex_index;
    topology.edge_index_buffer = buffers_.body_edge_index;
    topology.vertex_count = vertex_count_;
    topology.triangle_count = triangle_count_;
    return topology;
}

CharacterVertexBufferView CharacterGpuState::vertex_buffer_view() const
{
    CharacterVertexBufferView view;
    view.previous_position_buffer = buffers_.previous_position;
    view.current_position_buffer = buffers_.current_position;
    view.vertex_normal_buffer = buffers_.vertex_normal;
    view.vertex_count = vertex_count_;
    return view;
}

BodyTriangleResources CharacterGpuState::body_triangle_resources() const
{
    BodyTriangleResources resources;
    resources.position_buffer = buffers_.triangle_position;
    resources.normal_buffer = buffers_.triangle_normal;
    resources.triangle_count = triangle_count_;
    return resources;
}

CharacterNormalResources CharacterGpuState::mesh_normal_resources() const
{
    CharacterNormalResources resources;
    resources.triangle_normal_buffer = buffers_.triangle_normal;
    resources.vertex_normal_buffer = buffers_.vertex_normal;
    return resources;
}

BvhBufferView CharacterGpuState::body_triangle_bvh_buffer_view() const
{
    return {buffers_.body_triangle_bvh_node, buffers_.body_triangle_bounds, arm_triangle_ranges_};
}

BvhBufferView CharacterGpuState::body_vertex_bvh_buffer_view() const
{
    return {buffers_.body_vertex_bvh_node, buffers_.body_vertex_bounds};
}

BvhBufferView CharacterGpuState::body_edge_bvh_buffer_view() const
{
    return {buffers_.body_edge_bvh_node, buffers_.body_edge_bounds};
}

// Release

void CharacterGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_mesh_resources(gl);
    gl.glDeleteProgram(triangle_update_program_);
    gl.glDeleteProgram(position_program_);

    position_program_ = 0;
    triangle_update_program_ = 0;
    position_current_frame_base_location_ = -1;
    position_next_frame_base_location_ = -1;
    position_frame_alpha_location_ = -1;
    position_vertex_count_location_ = -1;
    triangle_count_location_ = -1;
}

void CharacterGpuState::release_mesh_resources(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.vertex_normal);
    gl.glDeleteBuffers(1, &buffers_.triangle_normal);
    gl.glDeleteBuffers(1, &buffers_.triangle_position);
    gl.glDeleteBuffers(1, &buffers_.adjacent_triangle_indices);
    gl.glDeleteBuffers(1, &buffers_.adjacent_triangle_offsets);
    gl.glDeleteBuffers(1, &buffers_.triangle_index);
    gl.glDeleteBuffers(1, &buffers_.body_triangle_bvh_node);
    gl.glDeleteBuffers(1, &buffers_.body_edge_bounds);
    gl.glDeleteBuffers(1, &buffers_.body_edge_index);
    gl.glDeleteBuffers(1, &buffers_.body_edge_bvh_node);
    gl.glDeleteBuffers(1, &buffers_.body_vertex_bounds);
    gl.glDeleteBuffers(1, &buffers_.body_vertex_bvh_vertex_index);
    gl.glDeleteBuffers(1, &buffers_.body_vertex_bvh_node);
    gl.glDeleteBuffers(1, &buffers_.body_triangle_bounds);
    gl.glDeleteBuffers(1, &buffers_.current_position);
    gl.glDeleteBuffers(1, &buffers_.previous_position);
    gl.glDeleteBuffers(1, &buffers_.all_frame_positions);
    gl.glDeleteVertexArrays(1, &vao_);

    reset_resources();
}

void CharacterGpuState::reset_resources() noexcept
{
    vao_ = 0;
    buffers_ = CharacterBufferSet{};
    frame_count_ = 0;
    vertex_count_ = 0;
    triangle_count_ = 0;
    arm_triangle_ranges_ = {};
    current_frame_index_ = 0;
    index_count_ = 0;
}
