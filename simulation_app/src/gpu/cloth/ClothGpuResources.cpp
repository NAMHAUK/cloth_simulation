#include "gpu/cloth/ClothGpuResources.h"

#include <cstdint>

ClothGpuResources::ClothGpuResources(ClothGpuResources&& other) noexcept
{
    take_gpu_resources_from(other);
}

ClothGpuResources& ClothGpuResources::operator=(ClothGpuResources&& other) noexcept
{
    if (this != &other) {
        take_gpu_resources_from(other);
    }

    return *this;
}

void ClothGpuResources::take_gpu_resources_from(ClothGpuResources& other) noexcept
{
    vao_ = other.vao_;
    rest_position_buffer_ = other.rest_position_buffer_;
    current_position_buffer_ = other.current_position_buffer_;
    previous_position_buffer_ = other.previous_position_buffer_;
    index_buffer_ = other.index_buffer_;
    index_count_ = other.index_count_;

    other.reset_resources();
}

void ClothGpuResources::reset_resources() noexcept
{
    vao_ = 0;
    rest_position_buffer_ = 0;
    current_position_buffer_ = 0;
    previous_position_buffer_ = 0;
    index_buffer_ = 0;
    index_count_ = 0;
}

bool ClothGpuResources::is_initialized() const
{
    return vao_ != 0 &&
           rest_position_buffer_ != 0 &&
           current_position_buffer_ != 0 &&
           previous_position_buffer_ != 0 &&
           index_buffer_ != 0 &&
           index_count_ > 0;
}

void ClothGpuResources::initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl)
{
    if (is_initialized()) {
        return;
    }

    release(gl);

    // buffer 생성 및 연결 설정
    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &rest_position_buffer_);
    gl.glCreateBuffers(1, &current_position_buffer_);
    gl.glCreateBuffers(1, &previous_position_buffer_);
    gl.glCreateBuffers(1, &index_buffer_);

    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(vao_, position_binding_index, current_position_buffer_, 0, 3 * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(vao_, position_attribute_location);
    gl.glVertexArrayAttribFormat(vao_, position_attribute_location, 3, GL_FLOAT, GL_FALSE, position_relative_offset);
    gl.glVertexArrayAttribBinding(vao_, position_attribute_location, position_binding_index);
    gl.glVertexArrayElementBuffer(vao_, index_buffer_);
}

void ClothGpuResources::upload(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl)
{
    if (garment_mesh.vertices.empty() || garment_mesh.indices.empty()) {
        return;
    }

    initialize_gpu_resources(gl);

    // buffer data upload
    const GLsizeiptr vertex_buffer_size = static_cast<GLsizeiptr>(garment_mesh.vertices.size() * sizeof(float));
    const GLsizeiptr index_buffer_size  = static_cast<GLsizeiptr>(garment_mesh.indices.size()  * sizeof(std::uint32_t));

    gl.glNamedBufferData(rest_position_buffer_      , vertex_buffer_size, garment_mesh.vertices.data(), GL_STATIC_DRAW);
    gl.glNamedBufferData(current_position_buffer_   , vertex_buffer_size, garment_mesh.vertices.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(previous_position_buffer_  , vertex_buffer_size, garment_mesh.vertices.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(index_buffer_              , index_buffer_size , garment_mesh.indices.data() , GL_STATIC_DRAW);

    index_count_ = static_cast<GLsizei>(garment_mesh.indices.size());
}

void ClothGpuResources::reset_states(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_initialized() || garment_mesh.vertices.empty()) {
        return;
    }

    const GLsizeiptr vertex_buffer_size = static_cast<GLsizeiptr>(garment_mesh.vertices.size() * sizeof(float));

    gl.glNamedBufferSubData(current_position_buffer_ , 0, vertex_buffer_size, garment_mesh.vertices.data());
    gl.glNamedBufferSubData(previous_position_buffer_, 0, vertex_buffer_size, garment_mesh.vertices.data());
}

void ClothGpuResources::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void ClothGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (index_buffer_ != 0) {
        gl.glDeleteBuffers(1, &index_buffer_);
    }
    if (previous_position_buffer_ != 0) {
        gl.glDeleteBuffers(1, &previous_position_buffer_);
    }
    if (current_position_buffer_ != 0) {
        gl.glDeleteBuffers(1, &current_position_buffer_);
    }
    if (rest_position_buffer_ != 0) {
        gl.glDeleteBuffers(1, &rest_position_buffer_);
    }
    if (vao_ != 0) {
        gl.glDeleteVertexArrays(1, &vao_);
    }

    reset_resources();
}
