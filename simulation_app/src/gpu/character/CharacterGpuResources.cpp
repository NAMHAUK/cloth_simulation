#include "gpu/character/CharacterGpuResources.h"

#include "asset/MeshGeometryUtils.h"
#include "gpu/bvh/BvhBuildUtils.h"
#include "utils/BufferUtils.h"
#include <cstddef>

namespace {
constexpr std::size_t endpoint_position_components = 4;
constexpr std::size_t character_triangle_geometry_components = 16;

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

bool is_uploadable_motion(const CharacterMotion& character_motion)
{
    return character_motion.frame_count > 0 &&
           character_motion.vertex_count > 0 &&
           character_motion.triangle_count > 0 &&
           character_motion.triangle_vertex_indices.size() ==
               static_cast<std::size_t>(character_motion.triangle_count) * 3u &&
           character_motion.vertices.size() >= frame_position_component_count(character_motion);
}
}

bool CharacterGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           frame_count_ > 0 &&
           vertex_count_ > 0 &&
           triangle_count_ > 0 &&
           index_count_ > 0;
}

void CharacterGpuResources::initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl)
{
    if (has_gpu_objects()) {
        return;
    }

    release(gl);

    // Create persistent OpenGL objects.
    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &buffers_.all_frame_position);
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
    gl.glCreateBuffers(1, &buffers_.triangle_geometry);
    gl.glCreateBuffers(1, &buffers_.vertex_normal);

    gl.glVertexArrayElementBuffer(vao_, buffers_.triangle_index);
}

void CharacterGpuResources::upload_motion(const CharacterMotion& character_motion,
                                          const Bvh& body_triangle_bvh,
                                          const Bvh& body_vertex_bvh,
                                          const Bvh& body_edge_bvh,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_uploadable_motion(character_motion)) {
        release(gl);
        return;
    }
    // 각 vertex에 인접한 triangle 정보 생성
    VertexTriangleAdjacency adjacency;
    if (!build_vertex_triangle_adjacency(character_motion.vertex_count,
                                         body_triangle_bvh.indices,
                                         adjacency)) {
        release(gl);
        return;
    }

    initialize_gpu_resources(gl);

    // GPU buffer 공간 생성 & 초기값 설정
    const GLsizeiptr position_bytes = byte_size<float>(frame_position_component_count(character_motion));
    const GLsizeiptr endpoint_position_bytes =
        byte_size<float>(vertex_position_component_count(character_motion.vertex_count));
    const std::uint32_t collision_triangle_count = bvh_build::leaf_element_count(body_triangle_bvh.nodes);
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
    const GLsizeiptr triangle_geometry_bytes = byte_size<float>(
        static_cast<std::size_t>(adjacency.triangle_count) * character_triangle_geometry_components);
    const GLsizeiptr vertex_normals_bytes =
        byte_size<float>(static_cast<std::size_t>(character_motion.vertex_count) * 4u);

    gl.glNamedBufferData(buffers_.all_frame_position,
                         position_bytes,
                         character_motion.vertices.data(),
                         GL_STATIC_DRAW);
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
    gl.glNamedBufferData(buffers_.triangle_geometry, triangle_geometry_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.vertex_normal, vertex_normals_bytes, nullptr, GL_DYNAMIC_DRAW);

    // 캐릭터 mesh GPU 초기값 설정
    frame_count_ = character_motion.frame_count;
    vertex_count_ = character_motion.vertex_count;
    triangle_count_ = adjacency.triangle_count;
    current_frame_index_ = 0;
    index_count_ = static_cast<GLsizei>(body_triangle_bvh.indices.size());
}

void CharacterGpuResources::set_current_frame(std::uint32_t frame_index)
{
    if (!is_initialized() || frame_index >= frame_count_) {
        return;
    }

    current_frame_index_ = frame_index;
}

void CharacterGpuResources::bind_current_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.current_position == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.current_position);
}

void CharacterGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.vertex_normal == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

std::uint32_t CharacterGpuResources::current_frame_index() const
{
    return current_frame_index_;
}

std::uint32_t CharacterGpuResources::next_frame_index() const
{
    if (frame_count_ == 0 || current_frame_index_ + 1u >= frame_count_) {
        return current_frame_index_;
    }

    return current_frame_index_ + 1u;
}

std::uint32_t CharacterGpuResources::frame_position_begin_index(std::uint32_t frame_index) const
{
    if (!is_initialized() || frame_index >= frame_count_) {
        return 0;
    }

    return frame_index * vertex_count_ * 3u;
}

std::uint32_t CharacterGpuResources::vertex_count() const
{
    return vertex_count_;
}

void CharacterGpuResources::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

CharacterMeshTopologyResources CharacterGpuResources::mesh_topology_resources() const
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

CharacterAnimationBufferView CharacterGpuResources::animation_buffer_view() const
{
    CharacterAnimationBufferView view;
    view.position_buffer = buffers_.all_frame_position;
    view.frame_count = frame_count_;
    view.vertex_count = vertex_count_;
    return view;
}

CharacterVertexBufferView CharacterGpuResources::character_vertex_buffer_view() const
{
    CharacterVertexBufferView view;
    view.previous_position_buffer = buffers_.previous_position;
    view.current_position_buffer = buffers_.current_position;
    view.vertex_normal_buffer = buffers_.vertex_normal;
    view.vertex_count = vertex_count_;
    return view;
}

TriangleGeometryResources CharacterGpuResources::character_triangle_geometry_resources() const
{
    TriangleGeometryResources resources;
    resources.triangle_geometry_buffer = buffers_.triangle_geometry;
    resources.triangle_count = triangle_count_;
    return resources;
}

CharacterNormalResources CharacterGpuResources::mesh_normal_resources() const
{
    CharacterNormalResources resources;
    resources.triangle_geometry_buffer = buffers_.triangle_geometry;
    resources.vertex_normal_buffer = buffers_.vertex_normal;
    resources.triangle_count = triangle_count_;
    return resources;
}

BvhBufferView CharacterGpuResources::body_triangle_bvh_buffer_view() const
{
    return {buffers_.body_triangle_bvh_node, buffers_.body_triangle_bounds};
}

BvhBufferView CharacterGpuResources::body_vertex_bvh_buffer_view() const
{
    return {buffers_.body_vertex_bvh_node, buffers_.body_vertex_bounds};
}

BvhBufferView CharacterGpuResources::body_edge_bvh_buffer_view() const
{
    return {buffers_.body_edge_bvh_node, buffers_.body_edge_bounds};
}

void CharacterGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.vertex_normal);
    gl.glDeleteBuffers(1, &buffers_.triangle_geometry);
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
    gl.glDeleteBuffers(1, &buffers_.all_frame_position);
    gl.glDeleteVertexArrays(1, &vao_);

    reset_resources();
}

void CharacterGpuResources::reset_resources() noexcept
{
    vao_ = 0;
    buffers_ = CharacterBufferSet{};
    frame_count_ = 0;
    vertex_count_ = 0;
    triangle_count_ = 0;
    current_frame_index_ = 0;
    index_count_ = 0;
}

bool CharacterGpuResources::has_gpu_objects() const
{
    return vao_ != 0 &&
           buffers_.all_frame_position != 0 &&
           buffers_.previous_position != 0 &&
           buffers_.current_position != 0 &&
           buffers_.triangle_index != 0 &&
           buffers_.body_triangle_bvh_node != 0 &&
           buffers_.body_triangle_bounds != 0 &&
           buffers_.body_vertex_bvh_node != 0 &&
           buffers_.body_vertex_bvh_vertex_index != 0 &&
           buffers_.body_vertex_bounds != 0 &&
           buffers_.body_edge_bvh_node != 0 &&
           buffers_.body_edge_index != 0 &&
           buffers_.body_edge_bounds != 0 &&
           buffers_.adjacent_triangle_offsets != 0 &&
           buffers_.adjacent_triangle_indices != 0 &&
           buffers_.triangle_geometry != 0 &&
           buffers_.vertex_normal != 0;
}
