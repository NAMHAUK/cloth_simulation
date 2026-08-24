#include "gpu/character/CharacterGpuState.h"

#include "asset/MeshGeometryUtils.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"
#include <cstddef>
#include <stdexcept>

namespace {
constexpr GLuint all_frame_positions_binding = 0;
constexpr GLuint current_positions_binding = 1;
constexpr GLuint triangle_position_binding = 0;
constexpr GLuint triangle_indices_binding = 1;
constexpr GLuint triangle_positions_binding = 2;
constexpr GLuint triangle_normals_binding = 3;
constexpr std::uint32_t position_update_local_size = 128;
constexpr std::uint32_t triangle_geometry_local_size = 128;
constexpr std::size_t endpoint_position_components = 4;
constexpr std::size_t triangle_position_components = 12;
constexpr std::size_t triangle_normal_components = 4;

std::size_t frame_position_component_count(const CharacterMotion& character_motion)
{
    return static_cast<std::size_t>(character_motion.frame_count) *
           static_cast<std::size_t>(character_motion.vertex_count) *
           position_components;
}

std::size_t vertex_position_component_count(std::uint32_t vertex_count)
{
    return static_cast<std::size_t>(vertex_count) * endpoint_position_components;
}
}

// Initialization

void CharacterGpuState::initialize(const std::filesystem::path& shader_dir,
                                   float body_detection_distance,
                                   QOpenGLFunctions_4_5_Core& gl)
{
    bvh_bounds_updater_.initialize(shader_dir, body_detection_distance, gl);

    const std::filesystem::path character_shader_dir = shader_dir / "character";
    position_program_ = load_compute_program(character_shader_dir / "character_vertex_position_update.comp",
                                             "Character vertex position update",
                                             gl);
    triangle_geometry_program_ =
        load_compute_program(character_shader_dir / "character_triangle_geometry_update.comp",
                             "Character triangle geometry update",
                             gl);
    position_current_frame_base_location_ = gl.glGetUniformLocation(position_program_, "uCurrentFrameBase");
    position_next_frame_base_location_ = gl.glGetUniformLocation(position_program_, "uNextFrameBase");
    position_frame_alpha_location_ = gl.glGetUniformLocation(position_program_, "uFrameAlpha");
    position_vertex_count_location_ = gl.glGetUniformLocation(position_program_, "uVertexCount");
    triangle_count_location_ = gl.glGetUniformLocation(triangle_geometry_program_, "uTriangleCount");

    if (position_current_frame_base_location_ < 0 ||
        position_next_frame_base_location_ < 0 ||
        position_frame_alpha_location_ < 0 ||
        position_vertex_count_location_ < 0 ||
        triangle_count_location_ < 0) {
        throw std::runtime_error("Character GPU state update compute shader missing required uniforms.");
    }

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

void CharacterGpuState::initialize_mesh(const CharacterMotion& character_motion,
                                        const Bvh& body_triangle_bvh,
                                        const Bvh& body_vertex_bvh,
                                        const Bvh& body_edge_bvh,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    VertexTriangleAdjacency adjacency;
    if (!build_vertex_triangle_adjacency(character_motion.vertex_count,
                                         body_triangle_bvh.indices,
                                         adjacency)) {
        return;
    }

    // GPU buffer 공간 생성 & 초기값 설정
    const GLsizeiptr endpoint_position_bytes =
        byte_size<float>(vertex_position_component_count(character_motion.vertex_count));
    const std::uint32_t collision_triangle_count = body_triangle_bvh.leaf_element_count();
    const auto edge_count = static_cast<std::uint32_t>(body_edge_bvh.indices.size() / 2u);
    const GLsizeiptr triangle_index_bytes = byte_size<std::uint32_t>(body_triangle_bvh.indices.size());
    const GLsizeiptr bvh_node_bytes = byte_size<BvhNode>(body_triangle_bvh.nodes.size());
    const GLsizeiptr body_triangle_bounds_bytes = byte_size<Aabb>(collision_triangle_count);
    const GLsizeiptr body_vertex_bvh_node_bytes = byte_size<BvhNode>(body_vertex_bvh.nodes.size());
    const GLsizeiptr body_vertex_bvh_vertex_index_bytes =
        byte_size<std::uint32_t>(body_vertex_bvh.indices.size());
    const GLsizeiptr body_vertex_bounds_bytes = byte_size<Aabb>(character_motion.vertex_count);
    const GLsizeiptr body_edge_bvh_node_bytes = byte_size<BvhNode>(body_edge_bvh.nodes.size());
    const GLsizeiptr body_edge_index_bytes = byte_size<std::uint32_t>(body_edge_bvh.indices.size());
    const GLsizeiptr body_edge_bounds_bytes = byte_size<Aabb>(edge_count);
    const GLsizeiptr adjacent_triangle_offsets_bytes = byte_size<std::uint32_t>(adjacency.offsets.size());
    const GLsizeiptr adjacent_triangle_indices_bytes =
        byte_size<std::uint32_t>(adjacency.triangle_indices.size());
    const GLsizeiptr triangle_position_bytes =
        byte_size<float>(static_cast<std::size_t>(adjacency.triangle_count) * triangle_position_components);
    const GLsizeiptr triangle_normal_bytes =
        byte_size<float>(static_cast<std::size_t>(adjacency.triangle_count) * triangle_normal_components);
    const GLsizeiptr vertex_normals_bytes =
        byte_size<float>(static_cast<std::size_t>(character_motion.vertex_count) * 4u);

    gl.glNamedBufferData(buffers_.previous_position, endpoint_position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.current_position, endpoint_position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_index,
                         triangle_index_bytes,
                         body_triangle_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bvh_node,
                         bvh_node_bytes,
                         body_triangle_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bounds, body_triangle_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_node,
                         body_vertex_bvh_node_bytes,
                         body_vertex_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_vertex_index,
                         body_vertex_bvh_vertex_index_bytes,
                         body_vertex_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bounds, body_vertex_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bvh_node,
                         body_edge_bvh_node_bytes,
                         body_edge_bvh.nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_index,
                         body_edge_index_bytes,
                         body_edge_bvh.indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bounds, body_edge_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_offsets,
                         adjacent_triangle_offsets_bytes,
                         adjacency.offsets.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_indices,
                         adjacent_triangle_indices_bytes,
                         adjacency.triangle_indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_position, triangle_position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_normal, triangle_normal_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.vertex_normal, vertex_normals_bytes, nullptr, GL_DYNAMIC_DRAW);

    // 캐릭터 mesh GPU 초기값 설정
    vertex_count_ = character_motion.vertex_count;
    triangle_count_ = adjacency.triangle_count;
    arm_triangle_ranges_ = body_triangle_bvh.arm_triangle_ranges;
    index_count_ = static_cast<GLsizei>(body_triangle_bvh.indices.size());
    bvh_bounds_updater_.set_level_offsets(body_triangle_bvh.level_offsets,
                                          body_vertex_bvh.level_offsets,
                                          body_edge_bvh.level_offsets);
}

// Motion update

void CharacterGpuState::set_motion(const CharacterMotion& character_motion, QOpenGLFunctions_4_5_Core& gl)
{
    if (character_motion.vertex_count != vertex_count_ ||
        character_motion.triangle_count != triangle_count_) {
        frame_count_ = 0;
        current_frame_index_ = 0;
        return;
    }

    const GLsizeiptr position_bytes = byte_size<float>(frame_position_component_count(character_motion));
    gl.glNamedBufferData(buffers_.all_frame_positions,
                         position_bytes,
                         character_motion.vertices.data(),
                         GL_STATIC_DRAW);

    frame_count_ = character_motion.frame_count;
    current_frame_index_ = 0;

    if (!has_motion()) {
        return;
    }

    // Initialization writes the selected pose to current, then mirrors it into previous.
    write_current_positions(0.0f, gl);
    copy_current_to_previous(gl);
    update_derived_pose(gl);
}

void CharacterGpuState::update_pose(std::uint32_t frame_index,
                                    float frame_alpha,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    if (!has_motion()) {
        return;
    }

    if (frame_index < frame_count_) {
        current_frame_index_ = frame_index;
    }

    // Continuous update carries old current into previous before writing the new current pose.
    copy_current_to_previous(gl);
    write_current_positions(frame_alpha, gl);
    update_derived_pose(gl);
}

void CharacterGpuState::write_current_positions(float frame_alpha, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.all_frame_positions == 0 || buffers_.current_position == 0 || vertex_count_ == 0) {
        return;
    }

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
    if (buffers_.previous_position == 0 || buffers_.current_position == 0 || vertex_count_ == 0) {
        return;
    }

    const GLsizeiptr position_bytes = byte_size<float>(vertex_position_component_count(vertex_count_));
    gl.glCopyNamedBufferSubData(buffers_.current_position, buffers_.previous_position, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

void CharacterGpuState::update_derived_pose(QOpenGLFunctions_4_5_Core& gl) const
{
    update_triangle_geometry(gl);
    bvh_bounds_updater_.update(mesh_topology_resources(),
                               vertex_buffer_view(),
                               body_triangle_resources(),
                               body_triangle_bvh_buffer_view(),
                               body_vertex_bvh_buffer_view(),
                               body_edge_bvh_buffer_view(),
                               gl);
}

void CharacterGpuState::update_triangle_geometry(QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.current_position == 0 ||
        buffers_.triangle_index == 0 ||
        buffers_.triangle_position == 0 ||
        buffers_.triangle_normal == 0 ||
        triangle_count_ == 0) {
        return;
    }

    gl.glUseProgram(triangle_geometry_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_position_binding, buffers_.current_position);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_indices_binding, buffers_.triangle_index);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_positions_binding, buffers_.triangle_position);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_normals_binding, buffers_.triangle_normal);
    gl.glProgramUniform1ui(triangle_geometry_program_, triangle_count_location_, triangle_count_);

    gl.glDispatchCompute(compute_group_count(triangle_count_, triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Rendering

void CharacterGpuState::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!has_motion()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void CharacterGpuState::bind_current_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.current_position == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.current_position);
}

void CharacterGpuState::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.vertex_normal == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

// State

bool CharacterGpuState::has_motion() const
{
    return frame_count_ > 0;
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
    bvh_bounds_updater_.release(gl);
    gl.glDeleteProgram(triangle_geometry_program_);
    gl.glDeleteProgram(position_program_);

    position_program_ = 0;
    triangle_geometry_program_ = 0;
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
