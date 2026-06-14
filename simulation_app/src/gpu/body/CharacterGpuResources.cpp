#include "gpu/body/CharacterGpuResources.h"

#include "asset/MeshGeometryUtils.h"
#include "gpu/collision/MeshBvhBuilder.h"
#include <cstddef>
#include <iostream>
#include <utility>

namespace {
constexpr std::size_t position_component_per_vertex = 3;
constexpr std::size_t character_triangle_geometry_components = 20;

std::size_t frame_position_component_count(const CharacterMesh& character_mesh)
{
    return static_cast<std::size_t>(character_mesh.frame_count) *
           static_cast<std::size_t>(character_mesh.vertex_count) *
           position_component_per_vertex;
}

bool is_uploadable_mesh(const CharacterMesh& character_mesh)
{
    return character_mesh.frame_count > 0 &&
           character_mesh.vertex_count > 0 &&
           !character_mesh.indices.empty() &&
           character_mesh.vertices.size() >= frame_position_component_count(character_mesh);
}
}

bool CharacterGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           frame_count_ > 0 &&
           vertex_count_ > 0 &&
           triangle_count_ > 0 &&
           bvh_node_count_ > 0 &&
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
    gl.glCreateBuffers(1, &all_frame_vertex_buffer_);
    gl.glCreateBuffers(1, &index_buffer_);
    gl.glCreateBuffers(1, &character_bvh_node_buffer_);
    gl.glCreateBuffers(1, &adjacent_triangle_offsets_);
    gl.glCreateBuffers(1, &adjacent_triangle_indices_);
    gl.glCreateBuffers(1, &character_triangle_geometry_buffer_);
    gl.glCreateBuffers(1, &vertex_normal_buffer_);

    gl.glVertexArrayElementBuffer(vao_, index_buffer_);
}

void CharacterGpuResources::upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_uploadable_mesh(character_mesh)) {
        release(gl);
        return;
    }

    // 각 vertex에 인접한 triangle 정보 생성
    const std::uint32_t source_triangle_count = static_cast<std::uint32_t>(character_mesh.indices.size() / 3u);
    MeshBvhBuilder bvh_builder(
        character_mesh.vertex_count,
        character_mesh.indices,
        character_mesh.vertices
    );
    MeshBvhBuildResult bvh_result = bvh_builder.build_mesh_bvh();
    if (!bvh_result.is_valid(source_triangle_count)) {
        std::cerr << "Failed to build character BVH.\n";
        release(gl);
        return;
    }

    VertexFaceAdjacency adjacency;
    if (!build_vertex_face_adjacency(character_mesh.vertex_count, bvh_result.triangle_indices, adjacency)) {
        release(gl);
        return;
    }

    initialize_gpu_resources(gl);

    // GPU buffer 공간 생성 & 초기값 설정
    const GLsizeiptr position_bytes = static_cast<GLsizeiptr>(frame_position_component_count(character_mesh) * sizeof(float));
    const GLsizeiptr index_bytes = static_cast<GLsizeiptr>(bvh_result.triangle_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr bvh_node_bytes = static_cast<GLsizeiptr>(bvh_result.nodes.size() * sizeof(MeshBvhNode));
    const GLsizeiptr adjacent_triangle_offsets_bytes = static_cast<GLsizeiptr>(adjacency.offsets.size() * sizeof(std::uint32_t));
    const GLsizeiptr adjacent_triangle_indices_bytes = static_cast<GLsizeiptr>(adjacency.face_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr triangle_geometry_bytes = static_cast<GLsizeiptr>(
        static_cast<std::size_t>(adjacency.face_count) * character_triangle_geometry_components * sizeof(float)
    );
    const GLsizeiptr vertex_normals_bytes = static_cast<GLsizeiptr>(character_mesh.vertex_count * 4u * sizeof(float));

    gl.glNamedBufferData(all_frame_vertex_buffer_, position_bytes, character_mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(index_buffer_, index_bytes, bvh_result.triangle_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(character_bvh_node_buffer_, bvh_node_bytes, bvh_result.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(adjacent_triangle_offsets_, adjacent_triangle_offsets_bytes, adjacency.offsets.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(adjacent_triangle_indices_, adjacent_triangle_indices_bytes, adjacency.face_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(character_triangle_geometry_buffer_, triangle_geometry_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(vertex_normal_buffer_, vertex_normals_bytes, nullptr, GL_DYNAMIC_DRAW);

    // 캐릭터 mesh GPU 초기값 설정
    frame_count_ = character_mesh.frame_count;
    vertex_count_ = character_mesh.vertex_count;
    triangle_count_ = adjacency.face_count;
    bvh_node_count_ = static_cast<std::uint32_t>(bvh_result.nodes.size());
    bvh_root_node_index_ = bvh_result.root_node_index;
    bvh_node_ranges_by_level_ = std::move(bvh_result.node_ranges_by_level);
    bvh_triangle_indices_ = std::move(bvh_result.triangle_indices);
    current_frame_index_ = 0;
    index_count_ = static_cast<GLsizei>(bvh_triangle_indices_.size());
}

void CharacterGpuResources::set_current_frame(std::uint32_t frame_index)
{
    if (!is_initialized() || frame_index >= frame_count_) {
        return;
    }

    current_frame_index_ = frame_index;
}

void CharacterGpuResources::bind_animation_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (all_frame_vertex_buffer_ == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, all_frame_vertex_buffer_);
}

void CharacterGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (vertex_normal_buffer_ == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, vertex_normal_buffer_);
}

std::uint32_t CharacterGpuResources::current_frame_index() const
{
    return current_frame_index_;
}

std::uint32_t CharacterGpuResources::vertex_count() const
{
    return vertex_count_;
}

const std::vector<std::uint32_t>& CharacterGpuResources::bvh_triangle_indices() const
{
    return bvh_triangle_indices_;
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
    topology.position_buffer = all_frame_vertex_buffer_;
    topology.index_buffer = index_buffer_;
    topology.adjacent_triangle_offsets_buffer = adjacent_triangle_offsets_;
    topology.adjacent_triangle_indices_buffer = adjacent_triangle_indices_;
    topology.position_component_offset = current_frame_index_ * vertex_count_ * 3u;
    topology.vertex_count = vertex_count_;
    topology.triangle_count = triangle_count_;
    return topology;
}

TriangleGeometryResources CharacterGpuResources::character_triangle_geometry_resources() const
{
    TriangleGeometryResources resources;
    resources.triangle_geometry_buffer = character_triangle_geometry_buffer_;
    resources.triangle_count = triangle_count_;
    return resources;
}

CharacterNormalResources CharacterGpuResources::mesh_normal_resources() const
{
    CharacterNormalResources resources;
    resources.triangle_geometry_buffer = character_triangle_geometry_buffer_;
    resources.vertex_normal_buffer = vertex_normal_buffer_;
    resources.triangle_count = triangle_count_;
    return resources;
}

MeshBvhResources CharacterGpuResources::character_bvh_resources() const
{
    MeshBvhResources resources;
    resources.node_buffer = character_bvh_node_buffer_;
    resources.node_count = bvh_node_count_;
    resources.root_node_index = bvh_root_node_index_;
    return resources;
}

const std::vector<BvhNodeRange>& CharacterGpuResources::bvh_node_ranges_by_level() const
{
    return bvh_node_ranges_by_level_;
}

void CharacterGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (vertex_normal_buffer_ != 0) {
        gl.glDeleteBuffers(1, &vertex_normal_buffer_);
    }
    if (character_triangle_geometry_buffer_ != 0) {
        gl.glDeleteBuffers(1, &character_triangle_geometry_buffer_);
    }
    if (adjacent_triangle_indices_ != 0) {
        gl.glDeleteBuffers(1, &adjacent_triangle_indices_);
    }
    if (adjacent_triangle_offsets_ != 0) {
        gl.glDeleteBuffers(1, &adjacent_triangle_offsets_);
    }
    if (index_buffer_ != 0) {
        gl.glDeleteBuffers(1, &index_buffer_);
    }
    if (character_bvh_node_buffer_ != 0) {
        gl.glDeleteBuffers(1, &character_bvh_node_buffer_);
    }
    if (all_frame_vertex_buffer_ != 0) {
        gl.glDeleteBuffers(1, &all_frame_vertex_buffer_);
    }
    if (vao_ != 0) {
        gl.glDeleteVertexArrays(1, &vao_);
    }

    reset_resources();
}

void CharacterGpuResources::reset_resources() noexcept
{
    vao_ = 0;
    all_frame_vertex_buffer_ = 0;
    index_buffer_ = 0;
    character_bvh_node_buffer_ = 0;
    adjacent_triangle_offsets_ = 0;
    adjacent_triangle_indices_ = 0;
    character_triangle_geometry_buffer_ = 0;
    vertex_normal_buffer_ = 0;
    frame_count_ = 0;
    vertex_count_ = 0;
    triangle_count_ = 0;
    bvh_node_count_ = 0;
    bvh_root_node_index_ = 0;
    bvh_node_ranges_by_level_.clear();
    bvh_triangle_indices_.clear();
    current_frame_index_ = 0;
    index_count_ = 0;
}

bool CharacterGpuResources::has_gpu_objects() const
{
    return vao_ != 0 &&
           all_frame_vertex_buffer_ != 0 &&
           index_buffer_ != 0 &&
           character_bvh_node_buffer_ != 0 &&
           adjacent_triangle_offsets_ != 0 &&
           adjacent_triangle_indices_ != 0 &&
           character_triangle_geometry_buffer_ != 0 &&
           vertex_normal_buffer_ != 0;
}
