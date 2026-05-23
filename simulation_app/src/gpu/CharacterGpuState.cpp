#include "gpu/CharacterGpuState.h"

#include <cstddef>

bool CharacterGpuState::is_initialized() const
{
    return vao_ != 0 &&
        all_frame_vertex_buffer_ != 0 &&
        index_buffer_ != 0 &&
        all_frame_vertex_buffer_size_ > 0 &&
        frame_count_ > 0 &&
        vertex_count_ > 0 &&
        index_count_ > 0;
}

void CharacterGpuState::initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl)
{
    if (vao_ != 0 && all_frame_vertex_buffer_ != 0 && index_buffer_ != 0) {
        return;
    }

    release(gl);

    // buffer 생성 & 값 연결 설정
    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &all_frame_vertex_buffer_);
    gl.glCreateBuffers(1, &index_buffer_);

    gl.glVertexArrayElementBuffer(vao_, index_buffer_);
}

void CharacterGpuState::upload_mesh(const CharacterMesh& character_mesh, QOpenGLFunctions_4_5_Core& gl)
{
    if (character_mesh.frame_count == 0 ||
        character_mesh.vertex_count == 0 ||
        character_mesh.indices.empty() ||
        character_mesh.vertices.empty()) {
        release(gl);
        return;
    }

    const std::size_t expected_position_component_count =
        static_cast<std::size_t>(character_mesh.frame_count) *
        static_cast<std::size_t>(character_mesh.vertex_count) * 3;

    if (character_mesh.vertices.size() < expected_position_component_count) {
        release(gl);
        return;
    }

    initialize_gpu_resources(gl);

    // buffer에 data upload
    all_frame_vertex_buffer_size_ = static_cast<GLsizeiptr>(expected_position_component_count * sizeof(float));
    index_buffer_size_ = static_cast<GLsizeiptr>(character_mesh.indices.size() * sizeof(std::uint32_t));

    gl.glNamedBufferData(all_frame_vertex_buffer_, all_frame_vertex_buffer_size_, character_mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(index_buffer_, index_buffer_size_, character_mesh.indices.data(), GL_STATIC_DRAW);

    frame_count_ = character_mesh.frame_count;
    vertex_count_ = character_mesh.vertex_count;
    current_frame_index_ = 0;
    index_count_ = static_cast<GLsizei>(character_mesh.indices.size());
}

void CharacterGpuState::set_current_frame(std::uint32_t frame_index)
{
    if (!is_initialized() || frame_index >= frame_count_) {
        return;
    }

    current_frame_index_ = frame_index;
}

void CharacterGpuState::bind_animation_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (all_frame_vertex_buffer_ == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, all_frame_vertex_buffer_);
}

std::uint32_t CharacterGpuState::current_frame_index() const
{
    return current_frame_index_;
}

std::uint32_t CharacterGpuState::vertex_count() const
{
    return vertex_count_;
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
    if (all_frame_vertex_buffer_ != 0) {
        gl.glDeleteBuffers(1, &all_frame_vertex_buffer_);
    }
    if (vao_ != 0) {
        gl.glDeleteVertexArrays(1, &vao_);
    }

    reset_resources();
}

void CharacterGpuState::reset_resources() noexcept
{
    vao_ = 0;
    all_frame_vertex_buffer_ = 0;
    index_buffer_ = 0;
    all_frame_vertex_buffer_size_ = 0;
    index_buffer_size_ = 0;
    frame_count_ = 0;
    vertex_count_ = 0;
    current_frame_index_ = 0;
    index_count_ = 0;
}
