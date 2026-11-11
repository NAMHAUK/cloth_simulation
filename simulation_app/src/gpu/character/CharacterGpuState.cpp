#include "gpu/character/CharacterGpuState.h"
#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"

#include <cstddef>
#include <vector>

#include <glm/vec4.hpp>

namespace {
constexpr std::size_t triangle_vertex_count = 3u;
}

// Initialization

void CharacterGpuState::initialize(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl)
{
    const CharacterMotion& motion = scene.character_motion();
    const TriangleBvh& body_triangle_bvh = scene.default_body_triangle_bvh();

    std::vector<glm::vec4> positions;
    positions.reserve(motion.vertex_count);
    for (std::uint32_t vertex = 0; vertex < motion.vertex_count; ++vertex) {
        positions.emplace_back(motion.vertex_positions[vertex], 0.0f);
    }

    const auto position_bytes = byte_size<glm::vec4>(motion.vertex_count);

    // create rendering buffers
    buffers_.current_position = create_buffer(position_bytes, positions.data(), GL_DYNAMIC_DRAW, gl);
    buffers_.triangle_vertex_indices = create_buffer(
        byte_size<TriangleVertexIndices>(body_triangle_bvh.triangle_vertex_indices.size()),
        body_triangle_bvh.triangle_vertex_indices.data(),
        GL_STATIC_DRAW,
        gl);
    buffers_.vertex_normal = create_buffer(byte_size<glm::vec4>(motion.vertex_count), nullptr, GL_DYNAMIC_DRAW, gl);
    
    gl.glClearNamedBufferData(buffers_.vertex_normal, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr);

    gl.glCreateVertexArrays(1, &vao_);
    gl.glVertexArrayElementBuffer(vao_, buffers_.triangle_vertex_indices);

    vertex_count_ = motion.vertex_count;
    triangle_count_ = static_cast<std::uint32_t>(body_triangle_bvh.triangle_vertex_indices.size());
    index_count_ = static_cast<GLsizei>(body_triangle_bvh.triangle_vertex_indices.size() * triangle_vertex_count);
}

// Rendering

void CharacterGpuState::draw(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindVertexArray(vao_);
    gl.glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

// Accessors

std::uint32_t CharacterGpuState::vertex_count() const
{
    return vertex_count_;
}

std::uint32_t CharacterGpuState::triangle_count() const
{
    return triangle_count_;
}

const CharacterRenderBuffers& CharacterGpuState::buffer_set() const
{
    return buffers_;
}

// Release

void CharacterGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.vertex_normal);
    gl.glDeleteBuffers(1, &buffers_.triangle_vertex_indices);
    gl.glDeleteBuffers(1, &buffers_.current_position);
    gl.glDeleteVertexArrays(1, &vao_);

    reset_resources();
}

void CharacterGpuState::reset_resources() noexcept
{
    vao_ = 0;
    buffers_ = CharacterRenderBuffers{};
    vertex_count_ = 0;
    triangle_count_ = 0;
    index_count_ = 0;
}
