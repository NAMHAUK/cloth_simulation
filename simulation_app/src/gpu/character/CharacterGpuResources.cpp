#include "gpu/character/CharacterGpuResources.h"

#include "asset/MeshGeometryUtils.h"
#include <cstddef>
#include <iostream>

namespace {
constexpr std::size_t vertex_position_components = 3;
constexpr std::size_t character_triangle_geometry_components = 16;
constexpr std::size_t primitive_bounds_components = 8;
constexpr std::size_t body_edge_bounds_components = 8;

std::size_t frame_position_component_count(const CharacterMesh& character_mesh)
{
    return static_cast<std::size_t>(character_mesh.frame_count) *
           static_cast<std::size_t>(character_mesh.vertex_count) *
           vertex_position_components;
}

std::size_t vertex_position_component_count(std::uint32_t vertex_count)
{
    return static_cast<std::size_t>(vertex_count) * vertex_position_components;
}

bool is_uploadable_mesh(const CharacterMesh& character_mesh)
{
    return character_mesh.frame_count > 0 &&
           character_mesh.vertex_count > 0 &&
           character_mesh.triangle_count > 0 &&
           character_mesh.triangle_vertex_indices.size() == static_cast<std::size_t>(character_mesh.triangle_count) * 3u &&
           character_mesh.vertices.size() >= frame_position_component_count(character_mesh);
}
}

bool CharacterGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           frame_count_ > 0 &&
           vertex_count_ > 0 &&
           triangle_count_ > 0 &&
           body_collision_triangle_count_ > 0 &&
           body_triangle_bvh_node_count_ > 0 &&
           body_vertex_bvh_node_count_ > 0 &&
           body_edge_bvh_node_count_ > 0 &&
           body_edge_count_ > 0 &&
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
    gl.glCreateBuffers(1, &buffers_.body_vertex_bvh_vertex_id);
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

void CharacterGpuResources::upload_mesh(const CharacterMesh& character_mesh,
                                        const TriangleBvhData& default_body_triangle_bvh_data,
                                        const VertexBvhData& default_body_vertex_bvh_data,
                                        const EdgeBvhData& default_body_edge_bvh_data,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_uploadable_mesh(character_mesh)) {
        release(gl);
        return;
    }
    if (!default_body_vertex_bvh_data.is_valid(character_mesh.vertex_count)) {
        std::cerr << "Invalid default body vertex BVH.\n";
        release(gl);
        return;
    }
    if (!default_body_edge_bvh_data.is_valid()) {
        std::cerr << "Invalid default body edge BVH.\n";
        release(gl);
        return;
    }

    // 각 vertex에 인접한 triangle 정보 생성
    if (!default_body_triangle_bvh_data.is_valid(character_mesh.triangle_count)) {
        std::cerr << "Invalid default body triangle BVH.\n";
        release(gl);
        return;
    }

    VertexFaceAdjacency adjacency;
    if (!build_vertex_face_adjacency(character_mesh.vertex_count, default_body_triangle_bvh_data.triangle_indices, adjacency)) {
        release(gl);
        return;
    }

    initialize_gpu_resources(gl);

    // GPU buffer 공간 생성 & 초기값 설정
    const GLsizeiptr position_bytes = static_cast<GLsizeiptr>(frame_position_component_count(character_mesh) * sizeof(float));
    const GLsizeiptr endpoint_position_bytes = static_cast<GLsizeiptr>(vertex_position_component_count(character_mesh.vertex_count) * sizeof(float));
    const GLsizeiptr triangle_index_bytes = static_cast<GLsizeiptr>(default_body_triangle_bvh_data.triangle_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr bvh_node_bytes = static_cast<GLsizeiptr>(default_body_triangle_bvh_data.nodes.size() * sizeof(BvhNode));
    const GLsizeiptr body_triangle_bounds_bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(default_body_triangle_bvh_data.collision_triangle_count) * primitive_bounds_components * sizeof(float));
    const GLsizeiptr body_vertex_bvh_node_bytes = static_cast<GLsizeiptr>(default_body_vertex_bvh_data.nodes.size() * sizeof(BvhNode));
    const GLsizeiptr body_vertex_bvh_vertex_id_bytes = static_cast<GLsizeiptr>(default_body_vertex_bvh_data.vertex_ids.size() * sizeof(std::uint32_t));
    const GLsizeiptr body_vertex_bounds_bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(character_mesh.vertex_count) * primitive_bounds_components * sizeof(float));
    const GLsizeiptr body_edge_bvh_node_bytes = static_cast<GLsizeiptr>(default_body_edge_bvh_data.nodes.size() * sizeof(BvhNode));
    const GLsizeiptr body_edge_index_bytes = static_cast<GLsizeiptr>(default_body_edge_bvh_data.edge_vertex_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr body_edge_bounds_bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(default_body_edge_bvh_data.edge_count()) * body_edge_bounds_components * sizeof(float));
    const GLsizeiptr adjacent_triangle_offsets_bytes = static_cast<GLsizeiptr>(adjacency.offsets.size() * sizeof(std::uint32_t));
    const GLsizeiptr adjacent_triangle_indices_bytes = static_cast<GLsizeiptr>(adjacency.face_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr triangle_geometry_bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(adjacency.face_count) * character_triangle_geometry_components * sizeof(float));
    const GLsizeiptr vertex_normals_bytes = static_cast<GLsizeiptr>(character_mesh.vertex_count * 4u * sizeof(float));

    gl.glNamedBufferData(buffers_.all_frame_position, position_bytes, character_mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.previous_position, endpoint_position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.current_position, endpoint_position_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_index, triangle_index_bytes, default_body_triangle_bvh_data.triangle_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bvh_node, bvh_node_bytes, default_body_triangle_bvh_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_triangle_bounds, body_triangle_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_node, body_vertex_bvh_node_bytes, default_body_vertex_bvh_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bvh_vertex_id, body_vertex_bvh_vertex_id_bytes, default_body_vertex_bvh_data.vertex_ids.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_vertex_bounds, body_vertex_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bvh_node, body_edge_bvh_node_bytes, default_body_edge_bvh_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_index, body_edge_index_bytes, default_body_edge_bvh_data.edge_vertex_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.body_edge_bounds, body_edge_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_offsets, adjacent_triangle_offsets_bytes, adjacency.offsets.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.adjacent_triangle_indices, adjacent_triangle_indices_bytes, adjacency.face_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers_.triangle_geometry, triangle_geometry_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers_.vertex_normal, vertex_normals_bytes, nullptr, GL_DYNAMIC_DRAW);

    // 캐릭터 mesh GPU 초기값 설정
    frame_count_ = character_mesh.frame_count;
    vertex_count_ = character_mesh.vertex_count;
    triangle_count_ = adjacency.face_count;
    body_collision_triangle_count_ = default_body_triangle_bvh_data.collision_triangle_count;
    body_triangle_bvh_node_count_ = static_cast<std::uint32_t>(default_body_triangle_bvh_data.nodes.size());
    body_vertex_bvh_node_count_ = static_cast<std::uint32_t>(default_body_vertex_bvh_data.nodes.size());
    body_edge_bvh_node_count_ = static_cast<std::uint32_t>(default_body_edge_bvh_data.nodes.size());
    body_edge_count_ = default_body_edge_bvh_data.edge_count();
    current_frame_index_ = 0;
    index_count_ = static_cast<GLsizei>(default_body_triangle_bvh_data.triangle_indices.size());
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

TriangleBvhResources CharacterGpuResources::body_triangle_bvh_resources() const
{
    TriangleBvhResources resources;
    resources.node_buffer = buffers_.body_triangle_bvh_node;
    resources.triangle_bounds_buffer = buffers_.body_triangle_bounds;
    resources.node_count = body_triangle_bvh_node_count_;
    resources.triangle_count = body_collision_triangle_count_;
    return resources;
}

VertexBvhResources CharacterGpuResources::body_vertex_bvh_resources() const
{
    VertexBvhResources resources;
    resources.node_buffer = buffers_.body_vertex_bvh_node;
    resources.vertex_id_buffer = buffers_.body_vertex_bvh_vertex_id;
    resources.vertex_bounds_buffer = buffers_.body_vertex_bounds;
    resources.node_count = body_vertex_bvh_node_count_;
    return resources;
}

EdgeBvhResources CharacterGpuResources::body_edge_bvh_resources() const
{
    EdgeBvhResources resources;
    resources.node_buffer = buffers_.body_edge_bvh_node;
    resources.edge_index_buffer = buffers_.body_edge_index;
    resources.edge_bounds_buffer = buffers_.body_edge_bounds;
    resources.node_count = body_edge_bvh_node_count_;
    resources.edge_count = body_edge_count_;
    return resources;
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
    gl.glDeleteBuffers(1, &buffers_.body_vertex_bvh_vertex_id);
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
    body_collision_triangle_count_ = 0;
    body_triangle_bvh_node_count_ = 0;
    body_vertex_bvh_node_count_ = 0;
    body_edge_bvh_node_count_ = 0;
    body_edge_count_ = 0;
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
           buffers_.body_vertex_bvh_vertex_id != 0 &&
           buffers_.body_vertex_bounds != 0 &&
           buffers_.body_edge_bvh_node != 0 &&
           buffers_.body_edge_index != 0 &&
           buffers_.body_edge_bounds != 0 &&
           buffers_.adjacent_triangle_offsets != 0 &&
           buffers_.adjacent_triangle_indices != 0 &&
           buffers_.triangle_geometry != 0 &&
           buffers_.vertex_normal != 0;
}
