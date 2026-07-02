#include "rendering/GroundGridMesh.h"

#include <cstddef>
#include <vector>

bool GroundGridMesh::is_initialized() const
{
    return vao_ != 0 && vertex_buffer_ != 0 && vertex_count_ > 0;
}

const glm::vec3& GroundGridMesh::color() const
{
    return color_;
}

void GroundGridMesh::upload(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr int half_line_count = 40;
    constexpr float spacing = 0.5f;
    constexpr float half_size = static_cast<float>(half_line_count) * spacing;

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>((half_line_count * 2 + 1) * 4 * 3));
    for (int line = -half_line_count; line <= half_line_count; ++line) {
        const float offset = static_cast<float>(line) * spacing;

        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(-half_size);
        vertices.push_back(offset);
        vertices.push_back(0.0f);
        vertices.push_back(half_size);

        vertices.push_back(-half_size);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
        vertices.push_back(half_size);
        vertices.push_back(0.0f);
        vertices.push_back(offset);
    }

    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &vertex_buffer_);
    vertex_count_ = static_cast<GLsizei>(vertices.size() / 3);

    gl.glNamedBufferData(vertex_buffer_, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);

    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(vao_, position_binding_index, vertex_buffer_, 0, 3 * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(vao_, position_attribute_location);
    gl.glVertexArrayAttribFormat(vao_, position_attribute_location, 3, GL_FLOAT, GL_FALSE, position_relative_offset);
    gl.glVertexArrayAttribBinding(vao_, position_attribute_location, position_binding_index);
}

void GroundGridMesh::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized()) {
        return;
    }

    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_LINES, 0, vertex_count_);
}

void GroundGridMesh::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &vertex_buffer_);
    gl.glDeleteVertexArrays(1, &vao_);

    vao_ = 0;
    vertex_buffer_ = 0;
    vertex_count_ = 0;
}
