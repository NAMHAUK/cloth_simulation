#include "gpu/body/CharacterGpuResources.h"

#include "asset/MeshGeometryUtils.h"
#include "gpu/scene/MeshBufferResources.h"

#include <cstddef>

namespace {
constexpr std::size_t position_component_per_vertex = 3;

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
    gl.glCreateBuffers(1, &adjacency_offset_buffer_);
    gl.glCreateBuffers(1, &adjacency_triangle_buffer_);
    gl.glCreateBuffers(1, &triangle_normal_buffer_);
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
    VertexFaceAdjacency adjacency;
    if (!build_vertex_face_adjacency(character_mesh.vertex_count, character_mesh.indices, adjacency)) {
        release(gl);
        return;
    }

    initialize_gpu_resources(gl);

    // GPU buffer 공간 생성 & 초기값 설정
    const GLsizeiptr position_bytes = static_cast<GLsizeiptr>(frame_position_component_count(character_mesh) * sizeof(float));
    const GLsizeiptr index_bytes = static_cast<GLsizeiptr>(character_mesh.indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr adjacency_offsets_bytes = static_cast<GLsizeiptr>(adjacency.offsets.size() * sizeof(std::uint32_t));
    const GLsizeiptr adjacency_triangles_bytes = static_cast<GLsizeiptr>(adjacency.face_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr triangle_normals_bytes = static_cast<GLsizeiptr>(adjacency.face_count * 4u * sizeof(float));
    const GLsizeiptr vertex_normals_bytes = static_cast<GLsizeiptr>(character_mesh.vertex_count * 4u * sizeof(float));

    gl.glNamedBufferData(all_frame_vertex_buffer_, position_bytes, character_mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(index_buffer_, index_bytes, character_mesh.indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(adjacency_offset_buffer_, adjacency_offsets_bytes, adjacency.offsets.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(adjacency_triangle_buffer_, adjacency_triangles_bytes, adjacency.face_indices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(triangle_normal_buffer_, triangle_normals_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(vertex_normal_buffer_, vertex_normals_bytes, nullptr, GL_DYNAMIC_DRAW);

    // 캐릭터 mesh GPU 초기값 설정
    frame_count_ = character_mesh.frame_count;
    vertex_count_ = character_mesh.vertex_count;
    triangle_count_ = adjacency.face_count;
    current_frame_index_ = 0;
    index_count_ = static_cast<GLsizei>(character_mesh.indices.size());
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

void CharacterGpuResources::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

MeshTopologyResources CharacterGpuResources::mesh_topology_resources() const
{
    MeshTopologyResources topology;
    topology.position_buffer = all_frame_vertex_buffer_;
    topology.index_buffer = index_buffer_;
    topology.adjacency_offset_buffer = adjacency_offset_buffer_;
    topology.adjacency_triangle_buffer = adjacency_triangle_buffer_;
    topology.position_component_offset = current_frame_index_ * vertex_count_ * 3u;
    topology.vertex_count = vertex_count_;
    topology.triangle_count = triangle_count_;
    return topology;
}

MeshNormalResources CharacterGpuResources::mesh_normal_resources() const
{
    MeshNormalResources normals;
    normals.triangle_normal_buffer = triangle_normal_buffer_;
    normals.vertex_normal_buffer = vertex_normal_buffer_;
    return normals;
}

void CharacterGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (vertex_normal_buffer_ != 0) {
        gl.glDeleteBuffers(1, &vertex_normal_buffer_);
    }
    if (triangle_normal_buffer_ != 0) {
        gl.glDeleteBuffers(1, &triangle_normal_buffer_);
    }
    if (adjacency_triangle_buffer_ != 0) {
        gl.glDeleteBuffers(1, &adjacency_triangle_buffer_);
    }
    if (adjacency_offset_buffer_ != 0) {
        gl.glDeleteBuffers(1, &adjacency_offset_buffer_);
    }
    if (index_buffer_ != 0) {
        gl.glDeleteBuffers(1, &index_buffer_);
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
    adjacency_offset_buffer_ = 0;
    adjacency_triangle_buffer_ = 0;
    triangle_normal_buffer_ = 0;
    vertex_normal_buffer_ = 0;
    frame_count_ = 0;
    vertex_count_ = 0;
    triangle_count_ = 0;
    current_frame_index_ = 0;
    index_count_ = 0;
}

bool CharacterGpuResources::has_gpu_objects() const
{
    return vao_ != 0 &&
           all_frame_vertex_buffer_ != 0 &&
           index_buffer_ != 0 &&
           adjacency_offset_buffer_ != 0 &&
           adjacency_triangle_buffer_ != 0 &&
           triangle_normal_buffer_ != 0 &&
           vertex_normal_buffer_ != 0;
}
