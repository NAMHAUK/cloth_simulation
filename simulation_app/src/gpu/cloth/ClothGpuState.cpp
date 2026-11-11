#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "utils/BufferUtils.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <glm/vec4.hpp>

namespace {
void upload_vertex_positions(const ClothRenderBuffers& buffers,
                             const std::vector<glm::vec3>& vertex_positions,
                             const GarmentBufferState& garment_state,
                             QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(garment_state.vertex_count);
    for (std::size_t index = 0; index < gpu_positions.size(); ++index) {
        gpu_positions[index] = glm::vec4(vertex_positions[index], 0.0f);
    }

    gl.glNamedBufferSubData(buffers.current_position,
                            byte_size<glm::vec4>(garment_state.vertex_start_index),
                            byte_size<glm::vec4>(garment_state.vertex_count),
                            gpu_positions.data());
}
}

// Buffer rebuild
std::vector<TriangleVertexIndices> ClothGpuState::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                                                  GarmentLayer changed_layer,
                                                                  QOpenGLFunctions_4_5_Core& gl)
{
    BufferState rebuild_state{};
    assign_garment_buffer_states(garments, rebuild_state);

    create_dynamic_buffers(garments, changed_layer, rebuild_state, gl);
    gl.glCreateBuffers(1, &rebuild_state.buffers.triangle_vertex_indices);

    auto triangle_vertex_indices = upload_topology(garments, rebuild_state, gl);

    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
    state_ = std::move(rebuild_state);
    configure_vao(gl);

    return triangle_vertex_indices;
}

std::vector<TriangleVertexIndices> ClothGpuState::create_simulation_buffers(const std::vector<GarmentObject>& garments,
                                                                            QOpenGLFunctions_4_5_Core& gl)
{
    auto triangle_vertex_indices = upload_topology(garments, state_, gl);

    if (!has_gpu_objects()) {
        throw std::runtime_error("Cannot prepare garment simulation: a cloth GPU object is missing.");
    }

    return triangle_vertex_indices;
}

void ClothGpuState::assign_garment_buffer_states(const std::vector<GarmentObject>& garments, BufferState& state)
{
    const auto append_elements = [](std::uint32_t& total_count, std::uint32_t count) {
        const std::uint32_t start_index = total_count;
        total_count += count;
        return start_index;
    };

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const auto& mesh = garment.mesh;
        auto& element_counts = state.element_counts;
        GarmentBufferState& garment_state = state.garments[layer];

        garment_state.vertex_count = mesh.vertex_positions.size();
        garment_state.vertex_start_index = append_elements(element_counts.vertex, garment_state.vertex_count);
        garment_state.triangle_count = mesh.triangle_vertex_indices.size();
        garment_state.triangle_start_index = append_elements(element_counts.triangle, garment_state.triangle_count);
        garment_state.stretch_constraint_count = mesh.stretch_constraints.colorized_edges.size();
        element_counts.stretch_constraint += garment_state.stretch_constraint_count;
        garment_state.bending_constraint_count = mesh.bending_constraints.colorized_edges.size();
        element_counts.bending_constraint += garment_state.bending_constraint_count;
    }
}

void ClothGpuState::create_dynamic_buffers(const std::vector<GarmentObject>& garments,
                                           GarmentLayer changed_layer,
                                           BufferState& rebuild_state,
                                           QOpenGLFunctions_4_5_Core& gl) const
{
    ClothRenderBuffers& buffers = rebuild_state.buffers;
    const ClothBufferElementCounts& counts = rebuild_state.element_counts;

    gl.glCreateVertexArrays(1, &buffers.vao);
    buffers.current_position = create_buffer(byte_size<glm::vec4>(counts.vertex), nullptr, GL_DYNAMIC_DRAW, gl);
    buffers.vertex_normal = create_buffer(byte_size<glm::vec4>(counts.vertex), nullptr, GL_DYNAMIC_DRAW, gl);

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const GarmentBufferState& garment_state = rebuild_state.garments[layer];

        if (changed_layer == layer) {
            upload_vertex_positions(rebuild_state.buffers, garment.mesh.vertex_positions, garment_state, gl);
        } else {
            gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            copy_current_positions(layer, rebuild_state, gl);
        }
    }
}

void ClothGpuState::copy_current_positions(GarmentLayer layer,
                                           const BufferState& rebuild_state,
                                           QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& source_state = state_.garments[layer];
    const GarmentBufferState& destination_state = rebuild_state.garments[layer];

    gl.glCopyNamedBufferSubData(state_.buffers.current_position,
                                rebuild_state.buffers.current_position,
                                byte_size<glm::vec4>(source_state.vertex_start_index),
                                byte_size<glm::vec4>(destination_state.vertex_start_index),
                                byte_size<glm::vec4>(destination_state.vertex_count));
}

std::vector<TriangleVertexIndices> ClothGpuState::upload_topology(const std::vector<GarmentObject>& garments,
                                                                  BufferState& rebuild_state,
                                                                  QOpenGLFunctions_4_5_Core& gl) const
{
    ClothRenderBuffers& buffers = rebuild_state.buffers;
    const ClothBufferElementCounts& counts = rebuild_state.element_counts;

    std::vector<TriangleVertexIndices> triangle_vertex_indices;
    triangle_vertex_indices.reserve(counts.triangle);

    for (const GarmentObject& garment : garments) {
        const std::uint32_t vertex_start_index = rebuild_state.garments[garment.layer].vertex_start_index;
        const auto& bvh_triangles = garment.triangle_bvh.triangle_vertex_indices;
        const auto& indices = bvh_triangles.empty() ? garment.mesh.triangle_vertex_indices : bvh_triangles;

        for (const TriangleVertexIndices& triangle : indices) {
            triangle_vertex_indices.push_back(
                {vertex_start_index + triangle.a, vertex_start_index + triangle.b, vertex_start_index + triangle.c});
        }
    }

    gl.glNamedBufferData(buffers.triangle_vertex_indices,
                         byte_size<TriangleVertexIndices>(triangle_vertex_indices.size()),
                         triangle_vertex_indices.data(),
                         GL_STATIC_DRAW);

    return triangle_vertex_indices;
}

void ClothGpuState::configure_vao(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(state_.buffers.vao,
                                 position_binding_index,
                                 state_.buffers.current_position,
                                 0,
                                 static_cast<GLsizei>(sizeof(glm::vec4)));
    gl.glEnableVertexArrayAttrib(state_.buffers.vao, position_attribute_location);
    gl.glVertexArrayAttribFormat(state_.buffers.vao,
                                 position_attribute_location,
                                 3,
                                 GL_FLOAT,
                                 GL_FALSE,
                                 position_relative_offset);
    gl.glVertexArrayAttribBinding(state_.buffers.vao, position_attribute_location, position_binding_index);
    gl.glVertexArrayElementBuffer(state_.buffers.vao, state_.buffers.triangle_vertex_indices);
}

// Garment updates
void ClothGpuState::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentLayer layer = garment.layer;
    const GarmentBufferState& garment_state = state_.garments[layer];
    upload_vertex_positions(state_.buffers, garment.mesh.vertex_positions, garment_state, gl);
}

// Base Position Buffers
void ClothGpuState::capture_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    clear_base_positions(gl);

    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);
    base_positions_ = create_buffer(position_bytes, nullptr, GL_DYNAMIC_COPY, gl);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.current_position, base_positions_, 0, 0, position_bytes);

    base_position_vertex_count_ = state_.element_counts.vertex;
}

void ClothGpuState::restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const
{
    if (base_positions_ == 0 || base_position_vertex_count_ != state_.element_counts.vertex) {
        throw std::runtime_error("Failed to restore garment base positions.");
    }

    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(base_positions_, state_.buffers.current_position, 0, 0, position_bytes);
}

void ClothGpuState::clear_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &base_positions_);

    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}

// Rendering
void ClothGpuState::draw_garment(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& garment_state = state_.garments[layer];
    const auto index_offset_bytes = garment_state.triangle_start_index * sizeof(TriangleVertexIndices);
    gl.glBindVertexArray(state_.buffers.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(garment_state.triangle_count * 3u),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// State
bool ClothGpuState::is_initialized() const
{
    if (state_.element_counts.stretch_constraint == 0 || state_.element_counts.bending_constraint == 0) {
        return false;
    }

    return has_gpu_objects() &&
           std::any_of(state_.garments.begin(),
                       state_.garments.end(),
                       [](const GarmentBufferState& garment_state) { return garment_state.vertex_count != 0u; }) &&
           state_.element_counts.vertex > 0 &&
           state_.element_counts.triangle > 0;
}

bool ClothGpuState::has_gpu_objects() const
{
    return state_.buffers.vao != 0 &&
           state_.buffers.current_position != 0 &&
           state_.buffers.triangle_vertex_indices != 0 &&
           state_.buffers.vertex_normal != 0;
}

// Accessors

const std::array<GarmentBufferState, garment_layer_count>& ClothGpuState::garment_states() const
{
    return state_.garments;
}

const ClothBufferElementCounts& ClothGpuState::element_counts() const
{
    return state_.element_counts;
}

const ClothRenderBuffers& ClothGpuState::buffer_set() const
{
    return state_.buffers;
}

// Release
void ClothGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
    reset_resources();
}

void ClothGpuState::delete_buffer_set(ClothRenderBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers.current_position,
        buffers.triangle_vertex_indices,
        buffers.vertex_normal,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);
    gl.glDeleteVertexArrays(1, &buffers.vao);

    buffers = {};
}

void ClothGpuState::reset_resources() noexcept
{
    state_ = {};
    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}
