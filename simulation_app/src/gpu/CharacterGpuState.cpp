#include "gpu/CharacterGpuState.h"

#include <cstddef>

bool CharacterGpuState::is_initialized() const
{
    return vao_ != 0 && vertex_buffer_ != 0 && index_buffer_ != 0 && index_count_ > 0;
}

void CharacterGpuState::initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl)
{
    if (vao_ != 0 && vertex_buffer_ != 0 && index_buffer_ != 0) {
        return;
    }

    release(gl);

    // buffer 생성 & 값 연결 설정
    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &vertex_buffer_);
    gl.glCreateBuffers(1, &index_buffer_);

    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(vao_, position_binding_index, vertex_buffer_, 0, 3 * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(vao_, position_attribute_location);
    gl.glVertexArrayAttribFormat(vao_, position_attribute_location, 3, GL_FLOAT, GL_FALSE,position_relative_offset);
    gl.glVertexArrayAttribBinding(vao_, position_attribute_location, position_binding_index);
    gl.glVertexArrayElementBuffer(vao_, index_buffer_);
}

void CharacterGpuState::upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl)
{
    if (character_mesh.vertex_count == 0 || character_mesh.indices.empty() || character_mesh.vertices.empty()) {
        return;
    }

    initialize_gpu_resources(gl);

    // buffer에 data upload
    vertex_buffer_size_ = static_cast<GLsizeiptr>(static_cast<std::size_t>(character_mesh.vertex_count) * 3 * sizeof(float));
    index_buffer_size_  = static_cast<GLsizeiptr>(character_mesh.indices.size() * sizeof(std::uint32_t));

    gl.glNamedBufferData(vertex_buffer_, vertex_buffer_size_, character_mesh.vertices.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(index_buffer_, index_buffer_size_, character_mesh.indices.data(), GL_STATIC_DRAW);

    index_count_ = static_cast<GLsizei>(character_mesh.indices.size());
}

void CharacterGpuState::upload_frame(const CharacterMesh& character_mesh,
                                     std::uint32_t frame_index,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || frame_index >= character_mesh.frame_count || character_mesh.vertex_count == 0 || vertex_buffer_size_ == 0) {
        return;
    }

    // buffer에 frame_index에 해당하는 vertex data만 upload
    const std::size_t frame_offset =
        static_cast<std::size_t>(frame_index) *
        static_cast<std::size_t>(character_mesh.vertex_count) * 3;

    gl.glNamedBufferSubData(vertex_buffer_, 0, vertex_buffer_size_, character_mesh.vertices.data() + frame_offset);
}

void CharacterGpuState::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void CharacterGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (index_buffer_ != 0) {
        gl.glDeleteBuffers(1, &index_buffer_);
    }
    if (vertex_buffer_ != 0) {
        gl.glDeleteBuffers(1, &vertex_buffer_);
    }
    if (vao_ != 0) {
        gl.glDeleteVertexArrays(1, &vao_);
    }

    reset_resources();
}

void CharacterGpuState::reset_resources() noexcept
{
    vao_ = 0;
    vertex_buffer_ = 0;
    index_buffer_ = 0;
    vertex_buffer_size_ = 0;
    index_buffer_size_ = 0;
    index_count_ = 0;
}
