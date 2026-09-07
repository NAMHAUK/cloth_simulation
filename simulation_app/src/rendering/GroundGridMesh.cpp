#include "rendering/GroundGridMesh.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace {
constexpr float grid_size = 40.0f;
constexpr float half_size = grid_size * 0.5f;
constexpr float spacing = 0.5f;
constexpr int line_count_per_axis = static_cast<int>(grid_size / spacing) + 1;
constexpr GLsizei grid_vertex_count = line_count_per_axis * 4;

std::vector<glm::vec3> build_ground_grid_vertices()
{
    std::vector<glm::vec3> vertices;
    vertices.reserve(static_cast<std::size_t>(grid_vertex_count));
    for (int line_index = 0; line_index < line_count_per_axis; ++line_index) {
        const float offset = -half_size + static_cast<float>(line_index) * spacing;
        vertices.insert(vertices.end(),
                        {{offset, 0.0f, -half_size},
                         {offset, 0.0f, half_size},
                         {-half_size, 0.0f, offset},
                         {half_size, 0.0f, offset}});
    }

    return vertices;
}
}

void GroundGridMesh::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    const std::vector<glm::vec3> vertices = build_ground_grid_vertices();

    gl.glCreateVertexArrays(1, &vao_);
    gl.glCreateBuffers(1, &vertex_buffer_);

    gl.glNamedBufferData(vertex_buffer_,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)),
                         vertices.data(),
                         GL_STATIC_DRAW);

    constexpr GLuint position_attribute_loc = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(vao_,
                                 position_binding_index,
                                 vertex_buffer_,
                                 0,
                                 static_cast<GLsizei>(sizeof(glm::vec3)));
    gl.glEnableVertexArrayAttrib(vao_, position_attribute_loc);
    gl.glVertexArrayAttribFormat(vao_,
                                 position_attribute_loc,
                                 3,
                                 GL_FLOAT,
                                 GL_FALSE,
                                 position_relative_offset);
    gl.glVertexArrayAttribBinding(vao_, position_attribute_loc, position_binding_index);
}

void GroundGridMesh::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    assert(vao_ != 0 && vertex_buffer_ != 0);

    gl.glBindVertexArray(vao_);
    gl.glDrawArrays(GL_LINES, 0, grid_vertex_count);
}

const glm::vec3& GroundGridMesh::color() const
{
    return color_;
}

void GroundGridMesh::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &vertex_buffer_);
    gl.glDeleteVertexArrays(1, &vao_);

    vao_ = 0;
    vertex_buffer_ = 0;
}
