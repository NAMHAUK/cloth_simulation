#include "gpu/cloth/ClothGpuResources.h"

#include "scene/SceneState.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr std::uint32_t gpu_position_components = 4;

// Buffer utilities
GLsizeiptr byte_size(std::uint32_t count, std::size_t element_size)
{
    return static_cast<GLsizeiptr>(std::size_t{count} * element_size);
}

void clear_dynamic_state(const ClothBufferSet& buffers, ElementRange vertices, QOpenGLFunctions_4_5_Core& gl)
{
    const std::array<GLuint, 3> state_buffers{
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
    };
    const GLsizeiptr offset = byte_size(vertices.offset, sizeof(glm::vec4));
    const GLsizeiptr size = byte_size(vertices.count, sizeof(glm::vec4));

    for (GLuint buffer : state_buffers) {
        gl.glClearNamedBufferSubData(buffer, GL_RGBA32F, offset, size, GL_RGBA, GL_FLOAT, nullptr);
    }
}

// Buffer allocation
ClothBufferSet create_buffer_set(const ClothBufferElementCounts& allocated_elements,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet buffers;
    const std::uint32_t allocated_attachment_constraints =
        std::max(allocated_elements.attachment_constraint, 1u);

    gl.glCreateVertexArrays(1, &buffers.vao);
    gl.glCreateBuffers(1, &buffers.current_position);
    gl.glCreateBuffers(1, &buffers.previous_position);
    gl.glCreateBuffers(1, &buffers.collision_pushout);
    gl.glCreateBuffers(1, &buffers.cloth_cloth_pushout);
    gl.glCreateBuffers(1, &buffers.contact_motion_delta);
    gl.glCreateBuffers(1, &buffers.body_triangle_id);
    gl.glCreateBuffers(1, &buffers.triangle_vertex_indices);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_offsets);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_indices);
    gl.glCreateBuffers(1, &buffers.stretch_edge_index);
    gl.glCreateBuffers(1, &buffers.stretch_rest_length);
    gl.glCreateBuffers(1, &buffers.bending_edge_index);
    gl.glCreateBuffers(1, &buffers.bending_rest_length);
    gl.glCreateBuffers(1, &buffers.attachment_indices);
    gl.glCreateBuffers(1, &buffers.attachment_barycentric_offset);
    gl.glCreateBuffers(1, &buffers.triangle_normal);
    gl.glCreateBuffers(1, &buffers.vertex_normal);

    const GLsizeiptr vertex_vec4_bytes = byte_size(allocated_elements.vertex, sizeof(glm::vec4));
    gl.glNamedBufferData(buffers.current_position, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.previous_position, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.collision_pushout, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.cloth_cloth_pushout, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.contact_motion_delta, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.body_triangle_id,
                         byte_size(allocated_elements.vertex, sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    const std::uint32_t invalid_body_triangle_id = std::numeric_limits<std::uint32_t>::max();
    gl.glClearNamedBufferData(buffers.body_triangle_id,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              &invalid_body_triangle_id);
    gl.glNamedBufferData(buffers.triangle_vertex_indices,
                         byte_size(allocated_elements.index, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_offsets,
                         byte_size(allocated_elements.vertex + 1u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_indices,
                         byte_size(allocated_elements.adjacency, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.stretch_edge_index,
                         byte_size(allocated_elements.stretch_constraint * 2u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.stretch_rest_length,
                         byte_size(allocated_elements.stretch_constraint, sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.bending_edge_index,
                         byte_size(allocated_elements.bending_constraint * 2u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.bending_rest_length,
                         byte_size(allocated_elements.bending_constraint, sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.attachment_indices,
                         byte_size(allocated_attachment_constraints, sizeof(glm::uvec2)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.attachment_barycentric_offset,
                         byte_size(allocated_attachment_constraints, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.triangle_normal,
                         byte_size(allocated_elements.triangle, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.vertex_normal, vertex_vec4_bytes, nullptr, GL_DYNAMIC_DRAW);

    return buffers;
}

// Buffer data
struct TopologyUploadData final
{
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint32_t> adjacent_triangle_offsets;
    std::vector<std::uint32_t> adjacent_triangle_indices;
};

// buffer에 upload할 topology data 생성
// garment 별로 있는 정보를 하나의 buffer에 upload하기 위해 모으는 과정
void build_topology_upload_data(const GarmentObject& garment,
                                ElementRange vertex_range,
                                ElementRange triangle_range,
                                ElementRange adjacency_range,
                                TopologyUploadData& data)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    // vertex index: 각 garment triangle의 vertex index
    for (std::uint32_t local_index : garment.mesh.triangle_vertex_indices) {
        data.vertex_indices.push_back(vertex_range.offset + local_index);
    }

    // adjacent_triangle_offsets: 각 vertex의 adjacent triangle 배열에서 시작 위치
    for (std::uint32_t local_vertex = 0; local_vertex <= vertex_range.count; ++local_vertex) {
        data.adjacent_triangle_offsets[vertex_range.offset + local_vertex] =
            adjacency_range.offset + adjacency.offsets[local_vertex];
    }

    // adjacent_triangle_indices: 각 vertex가 속한 triangle index 배열
    for (std::uint32_t local_face : adjacency.face_indices) {
        data.adjacent_triangle_indices.push_back(triangle_range.offset + local_face);
    }
}

// topology data를 GPU buffer에 upload
void upload_topology_data(const ClothBufferSet& buffers,
                          const TopologyUploadData& data,
                          QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        buffers.triangle_vertex_indices,
        0,
        byte_size(static_cast<std::uint32_t>(data.vertex_indices.size()), sizeof(std::uint32_t)),
        data.vertex_indices.data());
    gl.glNamedBufferSubData(
        buffers.adjacent_triangle_offsets,
        0,
        byte_size(static_cast<std::uint32_t>(data.adjacent_triangle_offsets.size()), sizeof(std::uint32_t)),
        data.adjacent_triangle_offsets.data());
    gl.glNamedBufferSubData(
        buffers.adjacent_triangle_indices,
        0,
        byte_size(static_cast<std::uint32_t>(data.adjacent_triangle_indices.size()), sizeof(std::uint32_t)),
        data.adjacent_triangle_indices.data());
}

struct DistanceConstraintUploadData final
{
    std::vector<std::uint32_t> edge_indices;
    std::vector<float> rest_lengths;
};

void build_distance_constraint_upload_data(const GarmentDistanceConstraints& constraints,
                                           ElementRange vertex_range,
                                           DistanceConstraintUploadData& data)
{
    for (const MeshEdge& edge : constraints.colorized_edges) {
        data.edge_indices.push_back(vertex_range.offset + edge.vertex_a);
        data.edge_indices.push_back(vertex_range.offset + edge.vertex_b);
    }

    data.rest_lengths.insert(data.rest_lengths.end(),
                             constraints.rest_lengths.begin(),
                             constraints.rest_lengths.end());
}

void upload_distance_constraint_data(GLuint edge_index_buffer,
                                     GLuint rest_length_buffer,
                                     std::uint32_t constraint_offset,
                                     const DistanceConstraintUploadData& data,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        edge_index_buffer,
        byte_size(constraint_offset * 2u, sizeof(std::uint32_t)),
        byte_size(static_cast<std::uint32_t>(data.edge_indices.size()), sizeof(std::uint32_t)),
        data.edge_indices.data());

    gl.glNamedBufferSubData(rest_length_buffer,
                            byte_size(constraint_offset, sizeof(float)),
                            byte_size(static_cast<std::uint32_t>(data.rest_lengths.size()), sizeof(float)),
                            data.rest_lengths.data());
}

void set_attachment_range(std::uint32_t constraint_offset,
                          std::uint32_t constraint_count,
                          std::vector<ElementRange>& attachment_ranges)
{
    const auto iter = std::find_if(
        attachment_ranges.begin(),
        attachment_ranges.end(),
        [constraint_offset](const ElementRange& range) { return range.offset == constraint_offset; });

    if (constraint_count == 0u) {
        if (iter != attachment_ranges.end()) {
            attachment_ranges.erase(iter);
        }
        return;
    }

    if (iter != attachment_ranges.end()) {
        iter->count = constraint_count;
        return;
    }

    attachment_ranges.push_back({constraint_offset, constraint_count});
}

void append_color_ranges(const std::vector<MeshElementRange>& local_ranges,
                         std::uint32_t element_offset,
                         std::vector<ElementRange>& color_ranges)
{
    for (const MeshElementRange& local_range : local_ranges) {
        color_ranges.push_back({element_offset + local_range.offset, local_range.count});
    }
}

void upload_position_data(const ClothBufferSet& buffers,
                          const std::vector<float>& vertices,
                          ElementRange vertex_range,
                          QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(vertex_range.count);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_range.count; ++vertex_index) {
        const std::size_t source_index = static_cast<std::size_t>(vertex_index) * position_components;
        gpu_positions[vertex_index] =
            glm::vec4(vertices[source_index], vertices[source_index + 1u], vertices[source_index + 2u], 0.0f);
    }

    const GLsizeiptr position_offset_bytes = byte_size(vertex_range.offset, sizeof(glm::vec4));
    const GLsizeiptr position_size_bytes = byte_size(vertex_range.count, sizeof(glm::vec4));

    gl.glNamedBufferSubData(buffers.current_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    gl.glNamedBufferSubData(buffers.previous_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    clear_dynamic_state(buffers, vertex_range, gl);
}

void upload_rest_length_data(const ClothBufferSet& buffers,
                             const GarmentObject& garment,
                             ElementRange stretch_constraint_range,
                             ElementRange bending_constraint_range,
                             QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
    const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;

    gl.glNamedBufferSubData(
        buffers.stretch_rest_length,
        byte_size(stretch_constraint_range.offset, sizeof(float)),
        byte_size(static_cast<std::uint32_t>(stretch_constraints.rest_lengths.size()), sizeof(float)),
        stretch_constraints.rest_lengths.data());

    gl.glNamedBufferSubData(
        buffers.bending_rest_length,
        byte_size(bending_constraint_range.offset, sizeof(float)),
        byte_size(static_cast<std::uint32_t>(bending_constraints.rest_lengths.size()), sizeof(float)),
        bending_constraints.rest_lengths.data());
}

struct BufferRebuildUploadData final
{
    TopologyUploadData topology;
    DistanceConstraintUploadData stretch_constraints;
    DistanceConstraintUploadData bending_constraints;
    std::vector<ElementRange> stretch_color_ranges;
    std::vector<ElementRange> bending_color_ranges;
    std::vector<ElementRange> attachment_ranges;
};

BufferRebuildUploadData prepare_buffer_rebuild_upload_data(const ClothBufferElementCounts& element_counts,
                                                           std::size_t garment_count)
{
    BufferRebuildUploadData data;
    data.topology.vertex_indices.reserve(element_counts.index);
    data.topology.adjacent_triangle_offsets.resize(static_cast<std::size_t>(element_counts.vertex) + 1u, 0);
    data.topology.adjacent_triangle_indices.reserve(element_counts.adjacency);
    data.stretch_constraints.edge_indices.reserve(element_counts.stretch_constraint * 2u);
    data.stretch_constraints.rest_lengths.reserve(element_counts.stretch_constraint);
    data.bending_constraints.edge_indices.reserve(element_counts.bending_constraint * 2u);
    data.bending_constraints.rest_lengths.reserve(element_counts.bending_constraint);
    data.stretch_color_ranges.reserve(garment_count * 8u);
    data.bending_color_ranges.reserve(garment_count * 8u);
    data.attachment_ranges.reserve(garment_count);
    return data;
}

// State preservation
void copy_dynamic_state_buffers(ElementRange source_vertex_range,
                                ElementRange destination_vertex_range,
                                const ClothBufferSet& source_buffers,
                                const ClothBufferSet& destination_buffers,
                                QOpenGLFunctions_4_5_Core& gl)
{
    const std::array<GLuint, 5> source_state_buffers{
        source_buffers.current_position,
        source_buffers.previous_position,
        source_buffers.collision_pushout,
        source_buffers.cloth_cloth_pushout,
        source_buffers.contact_motion_delta,
    };
    const std::array<GLuint, 5> destination_state_buffers{
        destination_buffers.current_position,
        destination_buffers.previous_position,
        destination_buffers.collision_pushout,
        destination_buffers.cloth_cloth_pushout,
        destination_buffers.contact_motion_delta,
    };
    const GLsizeiptr source_offset_bytes = byte_size(source_vertex_range.offset, sizeof(glm::vec4));
    const GLsizeiptr destination_offset_bytes = byte_size(destination_vertex_range.offset, sizeof(glm::vec4));
    const GLsizeiptr state_size_bytes = byte_size(destination_vertex_range.count, sizeof(glm::vec4));
    for (std::size_t buffer_index = 0; buffer_index < source_state_buffers.size(); ++buffer_index) {
        gl.glCopyNamedBufferSubData(source_state_buffers[buffer_index],
                                    destination_state_buffers[buffer_index],
                                    source_offset_bytes,
                                    destination_offset_bytes,
                                    state_size_bytes);
    }

    const GLsizeiptr source_body_triangle_id_offset_bytes =
        byte_size(source_vertex_range.offset, sizeof(std::uint32_t));
    const GLsizeiptr destination_body_triangle_id_offset_bytes =
        byte_size(destination_vertex_range.offset, sizeof(std::uint32_t));
    const GLsizeiptr body_triangle_id_bytes =
        byte_size(destination_vertex_range.count, sizeof(std::uint32_t));
    gl.glCopyNamedBufferSubData(source_buffers.body_triangle_id,
                                destination_buffers.body_triangle_id,
                                source_body_triangle_id_offset_bytes,
                                destination_body_triangle_id_offset_bytes,
                                body_triangle_id_bytes);
}

std::uint32_t copy_attachment_target_buffers(ElementRange old_attachment_constraint_range,
                                             ElementRange next_attachment_constraint_range,
                                             const std::vector<ElementRange>& active_attachment_ranges,
                                             const ClothBufferSet& old_buffers,
                                             const ClothBufferSet& next_buffers,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    const auto range_iter = std::find_if(active_attachment_ranges.begin(),
                                         active_attachment_ranges.end(),
                                         [old_attachment_constraint_range](const ElementRange& range) {
                                             return range.offset == old_attachment_constraint_range.offset;
                                         });
    if (range_iter == active_attachment_ranges.end()) {
        return 0u;
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    const GLsizeiptr old_index_offset_bytes =
        byte_size(old_attachment_constraint_range.offset, sizeof(glm::uvec2));
    const GLsizeiptr next_index_offset_bytes =
        byte_size(next_attachment_constraint_range.offset, sizeof(glm::uvec2));
    const GLsizeiptr index_size_bytes = byte_size(range_iter->count, sizeof(glm::uvec2));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_indices,
                                next_buffers.attachment_indices,
                                old_index_offset_bytes,
                                next_index_offset_bytes,
                                index_size_bytes);

    const GLsizeiptr old_barycentric_offset_bytes =
        byte_size(old_attachment_constraint_range.offset, sizeof(glm::vec4));
    const GLsizeiptr next_barycentric_offset_bytes =
        byte_size(next_attachment_constraint_range.offset, sizeof(glm::vec4));
    const GLsizeiptr barycentric_size_bytes = byte_size(range_iter->count, sizeof(glm::vec4));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_barycentric_offset,
                                next_buffers.attachment_barycentric_offset,
                                old_barycentric_offset_bytes,
                                next_barycentric_offset_bytes,
                                barycentric_size_bytes);

    return range_iter->count;
}

}

ClothGpuResources::ClothGpuResources(ClothGpuResources&& other) noexcept
{
    state_ = std::move(other.state_);
    base_positions_ = other.base_positions_;
    base_position_vertex_count_ = other.base_position_vertex_count_;

    other.reset_resources();
}

// Buffer management
void ClothGpuResources::assign_buffer_ranges(const std::vector<GarmentObject>& garments, BufferState& state)
{
    const auto append_range = [](std::uint32_t& used_count, std::size_t count) {
        const ElementRange range{used_count, static_cast<std::uint32_t>(count)};
        used_count += range.count;
        return range;
    };

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const auto& mesh = garment.mesh;
        auto& element_counts = state.element_counts;

        state.vertex_ranges[layer] =
            append_range(element_counts.vertex, mesh.vertices.size() / position_components);
        state.index_ranges[layer] = append_range(element_counts.index, mesh.triangle_vertex_indices.size());
        state.triangle_ranges[layer] = append_range(element_counts.triangle, mesh.adjacency.face_count);
        state.adjacency_ranges[layer] = append_range(element_counts.adjacency, mesh.adjacency.face_indices.size());
        state.stretch_constraint_ranges[layer] =
            append_range(element_counts.stretch_constraint, mesh.stretch_constraints.colorized_edges.size());
        state.bending_constraint_ranges[layer] =
            append_range(element_counts.bending_constraint, mesh.bending_constraints.colorized_edges.size());
        state.attachment_constraint_ranges[layer] =
            append_range(element_counts.attachment_constraint, mesh.attachment_vertex_indices.size());
    }
}

void ClothGpuResources::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                        GarmentLayer changed_layer,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    BufferState rebuild_state{};
    assign_buffer_ranges(garments, rebuild_state);
    rebuild_state.buffers = create_buffer_set(rebuild_state.element_counts, gl);

    build_buffer_state(garments, changed_layer, rebuild_state, gl);

    delete_gpu_objects(gl);
    state_ = std::move(rebuild_state);
    configure_vao(gl);
}

void ClothGpuResources::build_buffer_state(const std::vector<GarmentObject>& garments,
                                           GarmentLayer changed_layer,
                                           BufferState& rebuild_state,
                                           QOpenGLFunctions_4_5_Core& gl) const
{
    BufferRebuildUploadData upload_data =
        prepare_buffer_rebuild_upload_data(rebuild_state.element_counts, garments.size());

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const ElementRange vertex_range = rebuild_state.vertex_ranges[layer];
        const ElementRange old_vertex_range = state_.vertex_ranges[layer];
        const ElementRange attachment_constraint_range = rebuild_state.attachment_constraint_ranges[layer];
        const ElementRange old_attachment_constraint_range = state_.attachment_constraint_ranges[layer];
        const bool reset_garment = changed_layer == layer;

        if (reset_garment) {
            upload_position_data(rebuild_state.buffers, garment.mesh.vertices, vertex_range, gl);
        } else {
            copy_dynamic_state_buffers(old_vertex_range,
                                       vertex_range,
                                       state_.buffers,
                                       rebuild_state.buffers,
                                       gl);
        }

        std::uint32_t copied_attachment_target_count = 0u;
        if (!reset_garment) {
            copied_attachment_target_count = copy_attachment_target_buffers(old_attachment_constraint_range,
                                                                            attachment_constraint_range,
                                                                            state_.attachment_ranges,
                                                                            state_.buffers,
                                                                            rebuild_state.buffers,
                                                                            gl);
        }
        if (copied_attachment_target_count > 0u) {
            upload_data.attachment_ranges.push_back(
                {attachment_constraint_range.offset, copied_attachment_target_count});
        }

        const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
        const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;

        build_topology_upload_data(garment,
                                   vertex_range,
                                   rebuild_state.triangle_ranges[layer],
                                   rebuild_state.adjacency_ranges[layer],
                                   upload_data.topology);
        build_distance_constraint_upload_data(stretch_constraints,
                                              vertex_range,
                                              upload_data.stretch_constraints);
        build_distance_constraint_upload_data(bending_constraints,
                                              vertex_range,
                                              upload_data.bending_constraints);
        append_color_ranges(stretch_constraints.color_ranges,
                            rebuild_state.stretch_constraint_ranges[layer].offset,
                            upload_data.stretch_color_ranges);
        append_color_ranges(bending_constraints.color_ranges,
                            rebuild_state.bending_constraint_ranges[layer].offset,
                            upload_data.bending_color_ranges);
    }

    upload_topology_data(rebuild_state.buffers, upload_data.topology, gl);
    upload_distance_constraint_data(rebuild_state.buffers.stretch_edge_index,
                                    rebuild_state.buffers.stretch_rest_length,
                                    0,
                                    upload_data.stretch_constraints,
                                    gl);
    upload_distance_constraint_data(rebuild_state.buffers.bending_edge_index,
                                    rebuild_state.buffers.bending_rest_length,
                                    0,
                                    upload_data.bending_constraints,
                                    gl);

    rebuild_state.stretch_color_ranges = std::move(upload_data.stretch_color_ranges);
    rebuild_state.bending_color_ranges = std::move(upload_data.bending_color_ranges);
    rebuild_state.attachment_ranges = std::move(upload_data.attachment_ranges);
}

void ClothGpuResources::configure_vao(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(state_.buffers.vao,
                                 position_binding_index,
                                 state_.buffers.current_position,
                                 0,
                                 gpu_position_components * static_cast<GLsizei>(sizeof(float)));
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

void ClothGpuResources::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentLayer layer = garment.layer;
    upload_position_data(state_.buffers, garment.mesh.vertices, state_.vertex_ranges[layer], gl);
    upload_rest_length_data(state_.buffers,
                            garment,
                            state_.stretch_constraint_ranges[layer],
                            state_.bending_constraint_ranges[layer],
                            gl);
}

void ClothGpuResources::upload_garment_attachment_vertices(const GarmentObject& garment,
                                                           ElementRange& target_range,
                                                           QOpenGLFunctions_4_5_Core& gl)
{
    target_range = {};
    const GarmentLayer layer = garment.layer;
    const ElementRange vertex_range = state_.vertex_ranges[layer];
    const ElementRange attachment_constraint_range = state_.attachment_constraint_ranges[layer];
    const auto attachment_constraint_count =
        static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());
    target_range = {attachment_constraint_range.offset, attachment_constraint_count};
    if (attachment_constraint_count == 0u) {
        set_attachment_range(attachment_constraint_range.offset, 0u, state_.attachment_ranges);
        return;
    }

    std::vector<glm::uvec2> attachment_indices;
    attachment_indices.reserve(attachment_constraint_count);
    for (std::uint32_t local_vertex_index : garment.mesh.attachment_vertex_indices) {
        attachment_indices.push_back({vertex_range.offset + local_vertex_index, 0u});
    }

    gl.glNamedBufferSubData(state_.buffers.attachment_indices,
                            byte_size(attachment_constraint_range.offset, sizeof(glm::uvec2)),
                            byte_size(attachment_constraint_count, sizeof(glm::uvec2)),
                            attachment_indices.data());
}

void ClothGpuResources::activate_attachment_targets(const ElementRange& target_range)
{
    set_attachment_range(target_range.offset, target_range.count, state_.attachment_ranges);
}

void ClothGpuResources::capture_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    clear_base_positions(gl);

    const GLsizeiptr position_bytes = byte_size(state_.element_counts.vertex, sizeof(glm::vec4));
    gl.glCreateBuffers(1, &base_positions_);
    gl.glNamedBufferData(base_positions_, position_bytes, nullptr, GL_DYNAMIC_COPY);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.current_position, base_positions_, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    base_position_vertex_count_ = state_.element_counts.vertex;
}

bool ClothGpuResources::restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const
{
    if (base_positions_ == 0 || base_position_vertex_count_ != state_.element_counts.vertex) {
        return false;
    }

    const GLsizeiptr position_bytes = byte_size(state_.element_counts.vertex, sizeof(glm::vec4));

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(base_positions_, state_.buffers.current_position, 0, 0, position_bytes);
    gl.glCopyNamedBufferSubData(base_positions_, state_.buffers.previous_position, 0, 0, position_bytes);
    clear_dynamic_state(state_.buffers, {0, state_.element_counts.vertex}, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    return true;
}

void ClothGpuResources::clear_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &base_positions_);

    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}

void ClothGpuResources::copy_current_positions_to_previous(QOpenGLFunctions_4_5_Core& gl) const
{
    const GLsizeiptr position_bytes = byte_size(state_.element_counts.vertex, sizeof(glm::vec4));

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.current_position,
                                state_.buffers.previous_position,
                                0,
                                0,
                                position_bytes);
    clear_dynamic_state(state_.buffers, {0, state_.element_counts.vertex}, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Rendering
void ClothGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, state_.buffers.vertex_normal);
}

void ClothGpuResources::draw_garment(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const
{
    const ElementRange index_range = state_.index_ranges[layer];
    const auto index_offset_bytes = static_cast<std::uintptr_t>(index_range.offset) * sizeof(std::uint32_t);
    gl.glBindVertexArray(state_.buffers.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(index_range.count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// State
bool ClothGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           std::any_of(state_.vertex_ranges.begin(),
                       state_.vertex_ranges.end(),
                       [](ElementRange range) { return range.count != 0u; }) &&
           state_.element_counts.vertex > 0 &&
           state_.element_counts.index > 0 &&
           state_.element_counts.triangle > 0 &&
           state_.element_counts.stretch_constraint > 0 &&
           state_.element_counts.bending_constraint > 0 &&
           !state_.stretch_color_ranges.empty() &&
           !state_.bending_color_ranges.empty();
}

bool ClothGpuResources::has_gpu_objects() const
{
    return state_.buffers.vao != 0 &&
           state_.buffers.current_position != 0 &&
           state_.buffers.previous_position != 0 &&
           state_.buffers.collision_pushout != 0 &&
           state_.buffers.cloth_cloth_pushout != 0 &&
           state_.buffers.contact_motion_delta != 0 &&
           state_.buffers.body_triangle_id != 0 &&
           state_.buffers.triangle_vertex_indices != 0 &&
           state_.buffers.adjacent_triangle_offsets != 0 &&
           state_.buffers.adjacent_triangle_indices != 0 &&
           state_.buffers.stretch_edge_index != 0 &&
           state_.buffers.stretch_rest_length != 0 &&
           state_.buffers.bending_edge_index != 0 &&
           state_.buffers.bending_rest_length != 0 &&
           state_.buffers.attachment_indices != 0 &&
           state_.buffers.attachment_barycentric_offset != 0 &&
           state_.buffers.triangle_normal != 0 &&
           state_.buffers.vertex_normal != 0;
}

// Accessors

std::array<ElementRange, 2> ClothGpuResources::garment_vertex_ranges() const
{
    return state_.vertex_ranges;
}

ClothMotionBufferView ClothGpuResources::motion_buffer_view() const
{
    ClothMotionBufferView view;
    view.current_position_buffer = state_.buffers.current_position;
    view.previous_position_buffer = state_.buffers.previous_position;
    view.vertex_count = state_.element_counts.vertex;
    return view;
}

ClothCollisionPushoutBufferView ClothGpuResources::collision_pushout_buffer_view() const
{
    ClothCollisionPushoutBufferView view;
    view.collision_pushout_buffer = state_.buffers.collision_pushout;
    view.cloth_cloth_pushout_buffer = state_.buffers.cloth_cloth_pushout;
    view.vertex_count = state_.element_counts.vertex;
    return view;
}

ClothContactMotionBufferView ClothGpuResources::contact_motion_buffer_view() const
{
    ClothContactMotionBufferView view;
    view.contact_motion_delta_buffer = state_.buffers.contact_motion_delta;
    view.vertex_count = state_.element_counts.vertex;
    return view;
}

ClothBodyTriangleIdBufferView ClothGpuResources::body_triangle_id_buffer_view() const
{
    ClothBodyTriangleIdBufferView view;
    view.body_triangle_id_buffer = state_.buffers.body_triangle_id;
    view.vertex_count = state_.element_counts.vertex;
    return view;
}

DistanceConstraintBufferView ClothGpuResources::stretch_constraint_buffer_view() const
{
    DistanceConstraintBufferView view;
    view.edge_index_buffer = state_.buffers.stretch_edge_index;
    view.rest_length_buffer = state_.buffers.stretch_rest_length;
    view.constraint_count = state_.element_counts.stretch_constraint;
    view.color_ranges = &state_.stretch_color_ranges;
    return view;
}

DistanceConstraintBufferView ClothGpuResources::bending_constraint_buffer_view() const
{
    DistanceConstraintBufferView view;
    view.edge_index_buffer = state_.buffers.bending_edge_index;
    view.rest_length_buffer = state_.buffers.bending_rest_length;
    view.constraint_count = state_.element_counts.bending_constraint;
    view.color_ranges = &state_.bending_color_ranges;
    return view;
}

AttachmentConstraintBufferView ClothGpuResources::attachment_constraint_buffer_view() const
{
    AttachmentConstraintBufferView view;
    view.attachment_index_buffer = state_.buffers.attachment_indices;
    view.barycentric_offset_buffer = state_.buffers.attachment_barycentric_offset;
    view.constraint_count = state_.element_counts.attachment_constraint;
    view.ranges = &state_.attachment_ranges;
    return view;
}

ClothMeshTopologyResources ClothGpuResources::mesh_topology_resources() const
{
    ClothMeshTopologyResources topology;
    topology.position_buffer = state_.buffers.current_position;
    topology.triangle_index_buffer = state_.buffers.triangle_vertex_indices;
    topology.adjacent_triangle_offsets_buffer = state_.buffers.adjacent_triangle_offsets;
    topology.adjacent_triangle_indices_buffer = state_.buffers.adjacent_triangle_indices;
    topology.vertex_count = state_.element_counts.vertex;
    topology.triangle_count = state_.element_counts.triangle;
    return topology;
}

ClothNormalResources ClothGpuResources::mesh_normal_resources() const
{
    ClothNormalResources normals;
    normals.triangle_normal_buffer = state_.buffers.triangle_normal;
    normals.vertex_normal_buffer = state_.buffers.vertex_normal;
    return normals;
}

// Release
void ClothGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    reset_resources();
}

void ClothGpuResources::delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
}

void ClothGpuResources::delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers.current_position,
        buffers.previous_position,
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
        buffers.body_triangle_id,
        buffers.triangle_vertex_indices,
        buffers.adjacent_triangle_offsets,
        buffers.adjacent_triangle_indices,
        buffers.stretch_edge_index,
        buffers.stretch_rest_length,
        buffers.bending_edge_index,
        buffers.bending_rest_length,
        buffers.attachment_indices,
        buffers.attachment_barycentric_offset,
        buffers.triangle_normal,
        buffers.vertex_normal,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);
    gl.glDeleteVertexArrays(1, &buffers.vao);

    buffers = {};
}

void ClothGpuResources::reset_resources() noexcept
{
    state_ = {};
    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}
