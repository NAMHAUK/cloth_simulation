#include "gpu/cloth/ClothGpuResources.h"

#include "scene/SceneState.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr std::uint32_t normal_components = 4;
constexpr std::uint32_t gpu_position_components = 4;

// size / capacity //
GLsizeiptr float_byte_size(std::uint32_t count, std::uint32_t component_count)
{
    return static_cast<GLsizeiptr>(std::size_t{count} * component_count * sizeof(float));
}

GLsizeiptr scalar_byte_size(std::uint32_t count, std::size_t component_size)
{
    return static_cast<GLsizeiptr>(std::size_t{count} * component_size);
}

void clear_dynamic_state_range(const ClothBufferSet& buffers,
                               std::uint32_t vertex_offset,
                               std::uint32_t vertex_count,
                               QOpenGLFunctions_4_5_Core& gl)
{
    if (vertex_count == 0) {
        return;
    }

    gl.glClearNamedBufferSubData(buffers.velocity,
                                 GL_RGBA32F,
                                 float_byte_size(vertex_offset, gpu_position_components),
                                 float_byte_size(vertex_count, gpu_position_components),
                                 GL_RGBA,
                                 GL_FLOAT,
                                 nullptr);
    gl.glClearNamedBufferSubData(buffers.collision_pushout,
                                 GL_RGBA32F,
                                 scalar_byte_size(vertex_offset, sizeof(glm::vec4)),
                                 scalar_byte_size(vertex_count, sizeof(glm::vec4)),
                                 GL_RGBA,
                                 GL_FLOAT,
                                 nullptr);
    gl.glClearNamedBufferSubData(buffers.cloth_cloth_pushout,
                                 GL_RGBA32F,
                                 scalar_byte_size(vertex_offset, sizeof(glm::vec4)),
                                 scalar_byte_size(vertex_count, sizeof(glm::vec4)),
                                 GL_RGBA,
                                 GL_FLOAT,
                                 nullptr);
    gl.glClearNamedBufferSubData(buffers.contact_motion_delta,
                                 GL_RGBA32F,
                                 scalar_byte_size(vertex_offset, sizeof(glm::vec4)),
                                 scalar_byte_size(vertex_count, sizeof(glm::vec4)),
                                 GL_RGBA,
                                 GL_FLOAT,
                                 nullptr);
}

ClothBufferElementCounts make_expanded_capacity(const ClothBufferElementCounts& allocated_elements,
                                                const ClothBufferElementCounts& required_elements)
{
    const auto expand_count = [](std::uint32_t current_capacity, std::uint32_t required_capacity) {
        if (required_capacity <= current_capacity) {
            return current_capacity;
        }

        const std::uint32_t doubled_capacity =
            current_capacity > (std::numeric_limits<std::uint32_t>::max() / 2u)
                ? std::numeric_limits<std::uint32_t>::max()
                : current_capacity * 2u;
        return std::max(required_capacity, doubled_capacity);
    };

    return ClothBufferElementCounts{
        expand_count(allocated_elements.vertex, required_elements.vertex),
        expand_count(allocated_elements.index, required_elements.index),
        expand_count(allocated_elements.triangle, required_elements.triangle),
        expand_count(allocated_elements.adjacency_entry, required_elements.adjacency_entry),
        expand_count(allocated_elements.stretch_constraint, required_elements.stretch_constraint),
        expand_count(allocated_elements.bending_constraint, required_elements.bending_constraint),
        expand_count(allocated_elements.attachment_constraint, required_elements.attachment_constraint),
    };
}

// state checks //
bool is_uploadable_mesh(const GarmentMesh& garment_mesh)
{
    const auto vertex_count =
        static_cast<std::uint32_t>(garment_mesh.vertices.size() / vertex_position_components);

    return !garment_mesh.vertices.empty() &&
           !garment_mesh.triangle_vertex_indices.empty() &&
           garment_mesh.vertices.size() % vertex_position_components == 0u &&
           garment_mesh.triangle_vertex_indices.size() % 3u == 0u &&
           garment_mesh.adjacency.is_valid(vertex_count) &&
           garment_mesh.stretch_constraints.is_valid() &&
           garment_mesh.bending_constraints.is_valid();
}

bool has_valid_attachment_vertices(const GarmentObject& garment)
{
    const auto vertex_count =
        static_cast<std::uint32_t>(garment.mesh.vertices.size() / vertex_position_components);

    return std::all_of(garment.mesh.attachment_vertex_indices.begin(),
                       garment.mesh.attachment_vertex_indices.end(),
                       [vertex_count](std::uint32_t vertex_index) { return vertex_index < vertex_count; });
}

// garment range //
struct BufferRebuildGarmentData final
{
    const GarmentObject* garment = nullptr;
    GarmentBufferRanges buffer_ranges;
};

GarmentBufferRanges make_garment_buffer_ranges(const GarmentObject& garment,
                                               const ClothBufferElementCounts& offsets,
                                               std::uint32_t stretch_constraint_count,
                                               std::uint32_t bending_constraint_count,
                                               std::uint32_t attachment_constraint_count)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    GarmentBufferRanges buffer_ranges;
    buffer_ranges.id = garment.id;
    buffer_ranges.vertex_offset = offsets.vertex;
    buffer_ranges.vertex_count =
        static_cast<std::uint32_t>(garment.mesh.vertices.size() / vertex_position_components);
    buffer_ranges.index_offset = offsets.index;
    buffer_ranges.index_count = static_cast<std::uint32_t>(garment.mesh.triangle_vertex_indices.size());
    buffer_ranges.triangle_offset = offsets.triangle;
    buffer_ranges.triangle_count = adjacency.face_count;
    buffer_ranges.adjacency_entry_offset = offsets.adjacency_entry;
    buffer_ranges.adjacency_entry_count = static_cast<std::uint32_t>(adjacency.face_indices.size());
    buffer_ranges.stretch_constraint_offset = offsets.stretch_constraint;
    buffer_ranges.stretch_constraint_count = stretch_constraint_count;
    buffer_ranges.bending_constraint_offset = offsets.bending_constraint;
    buffer_ranges.bending_constraint_count = bending_constraint_count;
    buffer_ranges.attachment_constraint_offset = offsets.attachment_constraint;
    buffer_ranges.attachment_constraint_count = attachment_constraint_count;
    return buffer_ranges;
}

ClothBufferElementCounts make_next_used_elements(const ClothBufferElementCounts& used_elements,
                                                 const GarmentBufferRanges& appended_ranges)
{
    ClothBufferElementCounts next_used_elements = used_elements;
    next_used_elements.vertex += appended_ranges.vertex_count;
    next_used_elements.index += appended_ranges.index_count;
    next_used_elements.triangle += appended_ranges.triangle_count;
    next_used_elements.adjacency_entry += appended_ranges.adjacency_entry_count;
    next_used_elements.stretch_constraint += appended_ranges.stretch_constraint_count;
    next_used_elements.bending_constraint += appended_ranges.bending_constraint_count;
    next_used_elements.attachment_constraint += appended_ranges.attachment_constraint_count;
    return next_used_elements;
}

// buffer에서 garment이 사용할 구간 계산
bool assign_buffer_rebuild_ranges(const std::vector<GarmentObject>& garments,
                                  std::vector<BufferRebuildGarmentData>& rebuild_garments,
                                  ClothBufferElementCounts& rebuild_element_counts)
{
    rebuild_garments.clear();
    rebuild_garments.reserve(garments.size());
    rebuild_element_counts = {};

    // 각 garment에 대해 buffer에서 사용할 구간 계산 (누적 count를 통해 각 garment가 사용할 범위 저장)
    // rebuild_garments에 정보별 buffer에서 각 garment이 사용할 구간이 저장됨
    for (const GarmentObject& garment : garments) {
        if (!is_uploadable_mesh(garment.mesh)) {
            std::cerr << "Skipping invalid garment mesh for garment id " << garment.id << ".\n";
            continue;
        }

        const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
        const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;
        const auto attachment_constraint_count =
            static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());

        BufferRebuildGarmentData rebuild_garment;
        rebuild_garment.garment = &garment;
        rebuild_garment.buffer_ranges =
            make_garment_buffer_ranges(garment,
                                       rebuild_element_counts,
                                       static_cast<std::uint32_t>(stretch_constraints.colorized_edges.size()),
                                       static_cast<std::uint32_t>(bending_constraints.colorized_edges.size()),
                                       attachment_constraint_count);

        rebuild_element_counts.vertex += rebuild_garment.buffer_ranges.vertex_count;
        rebuild_element_counts.index += rebuild_garment.buffer_ranges.index_count;
        rebuild_element_counts.triangle += rebuild_garment.buffer_ranges.triangle_count;
        rebuild_element_counts.adjacency_entry += rebuild_garment.buffer_ranges.adjacency_entry_count;
        rebuild_element_counts.stretch_constraint += rebuild_garment.buffer_ranges.stretch_constraint_count;
        rebuild_element_counts.bending_constraint += rebuild_garment.buffer_ranges.bending_constraint_count;
        rebuild_element_counts.attachment_constraint +=
            rebuild_garment.buffer_ranges.attachment_constraint_count;

        rebuild_garments.push_back(std::move(rebuild_garment));
    }

    return !rebuild_garments.empty();
}

// topology upload //
struct TopologyUploadData final
{
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint32_t> adjacent_triangle_offsets;
    std::vector<std::uint32_t> adjacent_triangle_indices;
};

struct TopologyUploadOffsets final
{
    std::uint32_t index = 0;
    std::uint32_t adjacent_triangle_offsets = 0;
    std::uint32_t adjacent_triangle_indices = 0;
};

// buffer에 upload할 topology data 생성
// garment 별로 있는 정보를 하나의 buffer에 upload하기 위해 모으는 과정
void build_topology_upload_data(const GarmentObject& garment,
                                const GarmentBufferRanges& buffer_ranges,
                                std::uint32_t adjacent_triangle_offset_destination,
                                TopologyUploadData& data)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    // vertex index: 각 garment triangle의 vertex index
    for (std::uint32_t local_index : garment.mesh.triangle_vertex_indices) {
        data.vertex_indices.push_back(buffer_ranges.vertex_offset + local_index);
    }

    // adjacent_triangle_offsets: 각 vertex의 adjacent triangle 배열에서 시작 위치
    for (std::uint32_t local_vertex = 0; local_vertex <= buffer_ranges.vertex_count; ++local_vertex) {
        const std::uint32_t destination_vertex = adjacent_triangle_offset_destination + local_vertex;
        data.adjacent_triangle_offsets[destination_vertex] =
            buffer_ranges.adjacency_entry_offset + adjacency.offsets[local_vertex];
    }

    // adjacent_triangle_indices: 각 vertex가 속한 triangle index 배열
    for (std::uint32_t local_face : adjacency.face_indices) {
        data.adjacent_triangle_indices.push_back(buffer_ranges.triangle_offset + local_face);
    }
}

// topology data를 GPU buffer에 upload
void upload_topology_data(const ClothBufferSet& buffers,
                          const TopologyUploadOffsets& offsets,
                          const TopologyUploadData& data,
                          QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        buffers.index,
        scalar_byte_size(offsets.index, sizeof(std::uint32_t)),
        scalar_byte_size(static_cast<std::uint32_t>(data.vertex_indices.size()), sizeof(std::uint32_t)),
        data.vertex_indices.data());
    gl.glNamedBufferSubData(
        buffers.adjacent_triangle_offsets,
        scalar_byte_size(offsets.adjacent_triangle_offsets, sizeof(std::uint32_t)),
        scalar_byte_size(static_cast<std::uint32_t>(data.adjacent_triangle_offsets.size()),
                         sizeof(std::uint32_t)),
        data.adjacent_triangle_offsets.data());
    gl.glNamedBufferSubData(
        buffers.adjacent_triangle_indices,
        scalar_byte_size(offsets.adjacent_triangle_indices, sizeof(std::uint32_t)),
        scalar_byte_size(static_cast<std::uint32_t>(data.adjacent_triangle_indices.size()),
                         sizeof(std::uint32_t)),
        data.adjacent_triangle_indices.data());
}

// stretch constraint //
struct DistanceConstraintUploadData final
{
    std::vector<std::uint32_t> edge_indices;
    std::vector<float> rest_lengths;
};

struct AttachmentConstraintUploadData final
{
    std::vector<glm::uvec2> attachment_indices;
    std::vector<glm::vec4> barycentric_offsets;
};

void build_stretch_constraint_upload_data(const GarmentDistanceConstraints& stretch_constraints,
                                          const GarmentBufferRanges& buffer_ranges,
                                          DistanceConstraintUploadData& data)
{
    for (const MeshEdge& edge : stretch_constraints.colorized_edges) {
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_a);
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_b);
    }

    data.rest_lengths.insert(data.rest_lengths.end(),
                             stretch_constraints.rest_lengths.begin(),
                             stretch_constraints.rest_lengths.end());
}

void upload_stretch_constraints_data(const ClothBufferSet& buffers,
                                     std::uint32_t constraint_offset,
                                     const DistanceConstraintUploadData& data,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        buffers.stretch_edge_index,
        scalar_byte_size(constraint_offset * 2u, sizeof(std::uint32_t)),
        scalar_byte_size(static_cast<std::uint32_t>(data.edge_indices.size()), sizeof(std::uint32_t)),
        data.edge_indices.data());

    gl.glNamedBufferSubData(
        buffers.stretch_rest_length,
        scalar_byte_size(constraint_offset, sizeof(float)),
        scalar_byte_size(static_cast<std::uint32_t>(data.rest_lengths.size()), sizeof(float)),
        data.rest_lengths.data());
}

void build_bending_constraint_upload_data(const GarmentDistanceConstraints& bending_constraints,
                                          const GarmentBufferRanges& buffer_ranges,
                                          DistanceConstraintUploadData& data)
{
    for (const MeshEdge& edge : bending_constraints.colorized_edges) {
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_a);
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_b);
    }

    data.rest_lengths.insert(data.rest_lengths.end(),
                             bending_constraints.rest_lengths.begin(),
                             bending_constraints.rest_lengths.end());
}

void upload_bending_constraints_data(const ClothBufferSet& buffers,
                                     std::uint32_t constraint_offset,
                                     const DistanceConstraintUploadData& data,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        buffers.bending_edge_index,
        scalar_byte_size(constraint_offset * 2u, sizeof(std::uint32_t)),
        scalar_byte_size(static_cast<std::uint32_t>(data.edge_indices.size()), sizeof(std::uint32_t)),
        data.edge_indices.data());

    gl.glNamedBufferSubData(
        buffers.bending_rest_length,
        scalar_byte_size(constraint_offset, sizeof(float)),
        scalar_byte_size(static_cast<std::uint32_t>(data.rest_lengths.size()), sizeof(float)),
        data.rest_lengths.data());
}

void build_attachment_vertex_upload_data(const std::vector<std::uint32_t>& attachment_vertex_indices,
                                         const GarmentBufferRanges& buffer_ranges,
                                         AttachmentConstraintUploadData& data)
{
    for (std::uint32_t local_vertex_index : attachment_vertex_indices) {
        data.attachment_indices.push_back({buffer_ranges.vertex_offset + local_vertex_index, 0u});
        data.barycentric_offsets.push_back(glm::vec4(0.0f));
    }
}

void upload_attachment_constraints_data(const ClothBufferSet& buffers,
                                        std::uint32_t constraint_offset,
                                        const AttachmentConstraintUploadData& data,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    if (data.attachment_indices.empty()) {
        return;
    }

    gl.glNamedBufferSubData(
        buffers.attachment_indices,
        scalar_byte_size(constraint_offset, sizeof(glm::uvec2)),
        scalar_byte_size(static_cast<std::uint32_t>(data.attachment_indices.size()), sizeof(glm::uvec2)),
        data.attachment_indices.data());

    gl.glNamedBufferSubData(
        buffers.attachment_barycentric_offset,
        scalar_byte_size(constraint_offset, sizeof(glm::vec4)),
        scalar_byte_size(static_cast<std::uint32_t>(data.barycentric_offsets.size()), sizeof(glm::vec4)),
        data.barycentric_offsets.data());
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

// garment upload //
struct GarmentUploadData final
{
    TopologyUploadData topology;
    DistanceConstraintUploadData stretch_constraints;
    DistanceConstraintUploadData bending_constraints;
    AttachmentConstraintUploadData attachment_constraints;
};

GarmentUploadData prepare_garment_upload_data(const GarmentObject& garment,
                                              const GarmentBufferRanges& buffer_ranges)
{
    GarmentUploadData data;
    data.topology.vertex_indices.reserve(garment.mesh.triangle_vertex_indices.size());
    data.topology.adjacent_triangle_offsets.resize(static_cast<std::size_t>(buffer_ranges.vertex_count) + 1u,
                                                   0);
    data.topology.adjacent_triangle_indices.reserve(garment.mesh.adjacency.face_indices.size());
    data.stretch_constraints.edge_indices.reserve(buffer_ranges.stretch_constraint_count * 2u);
    data.stretch_constraints.rest_lengths.reserve(buffer_ranges.stretch_constraint_count);
    data.bending_constraints.edge_indices.reserve(buffer_ranges.bending_constraint_count * 2u);
    data.bending_constraints.rest_lengths.reserve(buffer_ranges.bending_constraint_count);
    data.attachment_constraints.attachment_indices.reserve(buffer_ranges.attachment_constraint_count);
    data.attachment_constraints.barycentric_offsets.reserve(buffer_ranges.attachment_constraint_count);
    return data;
}

void build_garment_upload_data(const GarmentObject& garment,
                               const GarmentBufferRanges& buffer_ranges,
                               GarmentUploadData& data)
{
    build_topology_upload_data(garment, buffer_ranges, 0, data.topology);
    build_stretch_constraint_upload_data(garment.mesh.stretch_constraints,
                                         buffer_ranges,
                                         data.stretch_constraints);
    build_bending_constraint_upload_data(garment.mesh.bending_constraints,
                                         buffer_ranges,
                                         data.bending_constraints);
    build_attachment_vertex_upload_data(garment.mesh.attachment_vertex_indices,
                                        buffer_ranges,
                                        data.attachment_constraints);
}

void upload_position_data(const ClothBufferSet& buffers,
                          const std::vector<float>& vertices,
                          const GarmentBufferRanges& buffer_ranges,
                          QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(buffer_ranges.vertex_count);
    for (std::uint32_t vertex_index = 0; vertex_index < buffer_ranges.vertex_count; ++vertex_index) {
        const std::size_t source_index = static_cast<std::size_t>(vertex_index) * vertex_position_components;
        gpu_positions[vertex_index] =
            glm::vec4(vertices[source_index], vertices[source_index + 1u], vertices[source_index + 2u], 0.0f);
    }

    const GLsizeiptr position_offset_bytes =
        float_byte_size(buffer_ranges.vertex_offset, gpu_position_components);
    const GLsizeiptr position_size_bytes =
        float_byte_size(buffer_ranges.vertex_count, gpu_position_components);

    gl.glNamedBufferSubData(buffers.current_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    gl.glNamedBufferSubData(buffers.previous_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    clear_dynamic_state_range(buffers, buffer_ranges.vertex_offset, buffer_ranges.vertex_count, gl);
}

void upload_rest_length_data(const ClothBufferSet& buffers,
                             const GarmentObject& garment,
                             const GarmentBufferRanges& buffer_ranges,
                             QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
    const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;

    gl.glNamedBufferSubData(
        buffers.stretch_rest_length,
        scalar_byte_size(buffer_ranges.stretch_constraint_offset, sizeof(float)),
        scalar_byte_size(static_cast<std::uint32_t>(stretch_constraints.rest_lengths.size()), sizeof(float)),
        stretch_constraints.rest_lengths.data());

    gl.glNamedBufferSubData(
        buffers.bending_rest_length,
        scalar_byte_size(buffer_ranges.bending_constraint_offset, sizeof(float)),
        scalar_byte_size(static_cast<std::uint32_t>(bending_constraints.rest_lengths.size()), sizeof(float)),
        bending_constraints.rest_lengths.data());
}

// buffer rebuild upload //
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
    data.topology.adjacent_triangle_indices.reserve(element_counts.adjacency_entry);
    data.stretch_constraints.edge_indices.reserve(element_counts.stretch_constraint * 2u);
    data.stretch_constraints.rest_lengths.reserve(element_counts.stretch_constraint);
    data.bending_constraints.edge_indices.reserve(element_counts.bending_constraint * 2u);
    data.bending_constraints.rest_lengths.reserve(element_counts.bending_constraint);
    data.stretch_color_ranges.reserve(garment_count * 8u);
    data.bending_color_ranges.reserve(garment_count * 8u);
    data.attachment_ranges.reserve(garment_count);
    return data;
}

void build_buffer_rebuild_upload_data(const GarmentObject& garment,
                                      const GarmentBufferRanges& buffer_ranges,
                                      BufferRebuildUploadData& data)
{
    const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
    const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;

    build_topology_upload_data(garment, buffer_ranges, buffer_ranges.vertex_offset, data.topology);
    build_stretch_constraint_upload_data(stretch_constraints, buffer_ranges, data.stretch_constraints);
    build_bending_constraint_upload_data(bending_constraints, buffer_ranges, data.bending_constraints);

    append_color_ranges(stretch_constraints.color_ranges,
                        buffer_ranges.stretch_constraint_offset,
                        data.stretch_color_ranges);
    append_color_ranges(bending_constraints.color_ranges,
                        buffer_ranges.bending_constraint_offset,
                        data.bending_color_ranges);
}

// buffer object //
ClothBufferSet create_buffer_set(const ClothBufferElementCounts& allocated_elements,
                                 QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet buffers;
    const std::uint32_t allocated_attachment_constraints =
        std::max(allocated_elements.attachment_constraint, 1u);

    gl.glCreateVertexArrays(1, &buffers.vao);
    gl.glCreateBuffers(1, &buffers.current_position);
    gl.glCreateBuffers(1, &buffers.previous_position);
    gl.glCreateBuffers(1, &buffers.velocity);
    gl.glCreateBuffers(1, &buffers.collision_pushout);
    gl.glCreateBuffers(1, &buffers.cloth_cloth_pushout);
    gl.glCreateBuffers(1, &buffers.contact_motion_delta);
    gl.glCreateBuffers(1, &buffers.body_triangle_id);
    gl.glCreateBuffers(1, &buffers.index);
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

    gl.glNamedBufferData(buffers.current_position,
                         float_byte_size(allocated_elements.vertex, gpu_position_components),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.previous_position,
                         float_byte_size(allocated_elements.vertex, gpu_position_components),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.velocity,
                         float_byte_size(allocated_elements.vertex, gpu_position_components),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.collision_pushout,
                         scalar_byte_size(allocated_elements.vertex, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.cloth_cloth_pushout,
                         scalar_byte_size(allocated_elements.vertex, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.contact_motion_delta,
                         scalar_byte_size(allocated_elements.vertex, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.body_triangle_id,
                         scalar_byte_size(allocated_elements.vertex, sizeof(std::uint32_t)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    const std::uint32_t invalid_body_triangle_id = std::numeric_limits<std::uint32_t>::max();
    gl.glClearNamedBufferData(buffers.body_triangle_id,
                              GL_R32UI,
                              GL_RED_INTEGER,
                              GL_UNSIGNED_INT,
                              &invalid_body_triangle_id);
    gl.glNamedBufferData(buffers.index,
                         scalar_byte_size(allocated_elements.index, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_offsets,
                         scalar_byte_size(allocated_elements.vertex + 1u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_indices,
                         scalar_byte_size(allocated_elements.adjacency_entry, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.stretch_edge_index,
                         scalar_byte_size(allocated_elements.stretch_constraint * 2u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.stretch_rest_length,
                         scalar_byte_size(allocated_elements.stretch_constraint, sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.bending_edge_index,
                         scalar_byte_size(allocated_elements.bending_constraint * 2u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.bending_rest_length,
                         scalar_byte_size(allocated_elements.bending_constraint, sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.attachment_indices,
                         scalar_byte_size(allocated_attachment_constraints, sizeof(glm::uvec2)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.attachment_barycentric_offset,
                         scalar_byte_size(allocated_attachment_constraints, sizeof(glm::vec4)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.triangle_normal,
                         float_byte_size(allocated_elements.triangle, normal_components),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.vertex_normal,
                         float_byte_size(allocated_elements.vertex, normal_components),
                         nullptr,
                         GL_DYNAMIC_DRAW);

    return buffers;
}

void copy_used_buffer_data(const ClothBufferSet& old_buffers,
                           const ClothBufferSet& next_buffers,
                           const ClothBufferElementCounts& used_elements,
                           QOpenGLFunctions_4_5_Core& gl)
{
    if (old_buffers.current_position == 0) {
        return;
    }

    const GLsizeiptr position_bytes = float_byte_size(used_elements.vertex, gpu_position_components);
    const GLsizeiptr vertex_state_bytes = scalar_byte_size(used_elements.vertex, sizeof(glm::vec4));
    const GLsizeiptr body_triangle_id_bytes = scalar_byte_size(used_elements.vertex, sizeof(std::uint32_t));
    const GLsizeiptr index_bytes = scalar_byte_size(used_elements.index, sizeof(std::uint32_t));
    const GLsizeiptr adjacent_triangle_offsets_bytes =
        scalar_byte_size(used_elements.vertex + 1u, sizeof(std::uint32_t));
    const GLsizeiptr adjacent_triangle_indices_bytes =
        scalar_byte_size(used_elements.adjacency_entry, sizeof(std::uint32_t));
    const GLsizeiptr stretch_edge_index_bytes =
        scalar_byte_size(used_elements.stretch_constraint * 2u, sizeof(std::uint32_t));
    const GLsizeiptr stretch_rest_length_bytes =
        scalar_byte_size(used_elements.stretch_constraint, sizeof(float));
    const GLsizeiptr bending_edge_index_bytes =
        scalar_byte_size(used_elements.bending_constraint * 2u, sizeof(std::uint32_t));
    const GLsizeiptr bending_rest_length_bytes =
        scalar_byte_size(used_elements.bending_constraint, sizeof(float));
    const GLsizeiptr attachment_index_bytes =
        scalar_byte_size(used_elements.attachment_constraint, sizeof(glm::uvec2));
    const GLsizeiptr attachment_barycentric_offset_bytes =
        scalar_byte_size(used_elements.attachment_constraint, sizeof(glm::vec4));

    if (position_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.current_position,
                                    next_buffers.current_position,
                                    0,
                                    0,
                                    position_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.previous_position,
                                    next_buffers.previous_position,
                                    0,
                                    0,
                                    position_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.velocity, next_buffers.velocity, 0, 0, position_bytes);
    }
    if (vertex_state_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.collision_pushout,
                                    next_buffers.collision_pushout,
                                    0,
                                    0,
                                    vertex_state_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.cloth_cloth_pushout,
                                    next_buffers.cloth_cloth_pushout,
                                    0,
                                    0,
                                    vertex_state_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.contact_motion_delta,
                                    next_buffers.contact_motion_delta,
                                    0,
                                    0,
                                    vertex_state_bytes);
    }
    if (body_triangle_id_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.body_triangle_id,
                                    next_buffers.body_triangle_id,
                                    0,
                                    0,
                                    body_triangle_id_bytes);
    }
    if (index_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.index, next_buffers.index, 0, 0, index_bytes);
    }
    if (adjacent_triangle_offsets_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.adjacent_triangle_offsets,
                                    next_buffers.adjacent_triangle_offsets,
                                    0,
                                    0,
                                    adjacent_triangle_offsets_bytes);
    }
    if (adjacent_triangle_indices_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.adjacent_triangle_indices,
                                    next_buffers.adjacent_triangle_indices,
                                    0,
                                    0,
                                    adjacent_triangle_indices_bytes);
    }
    if (stretch_edge_index_bytes > 0 && old_buffers.stretch_edge_index != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.stretch_edge_index,
                                    next_buffers.stretch_edge_index,
                                    0,
                                    0,
                                    stretch_edge_index_bytes);
    }
    if (stretch_rest_length_bytes > 0 && old_buffers.stretch_rest_length != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.stretch_rest_length,
                                    next_buffers.stretch_rest_length,
                                    0,
                                    0,
                                    stretch_rest_length_bytes);
    }
    if (bending_edge_index_bytes > 0 && old_buffers.bending_edge_index != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.bending_edge_index,
                                    next_buffers.bending_edge_index,
                                    0,
                                    0,
                                    bending_edge_index_bytes);
    }
    if (bending_rest_length_bytes > 0 && old_buffers.bending_rest_length != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.bending_rest_length,
                                    next_buffers.bending_rest_length,
                                    0,
                                    0,
                                    bending_rest_length_bytes);
    }
    if (attachment_index_bytes > 0 && old_buffers.attachment_indices != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.attachment_indices,
                                    next_buffers.attachment_indices,
                                    0,
                                    0,
                                    attachment_index_bytes);
    }
    if (attachment_barycentric_offset_bytes > 0 && old_buffers.attachment_barycentric_offset != 0) {
        gl.glCopyNamedBufferSubData(old_buffers.attachment_barycentric_offset,
                                    next_buffers.attachment_barycentric_offset,
                                    0,
                                    0,
                                    attachment_barycentric_offset_bytes);
    }
}

bool copy_dynamic_state_buffers(const GarmentBufferRanges& old_data,
                                const GarmentBufferRanges& next_data,
                                const ClothBufferSet& old_buffers,
                                const ClothBufferSet& next_buffers,
                                QOpenGLFunctions_4_5_Core& gl)
{
    if (old_data.vertex_count != next_data.vertex_count ||
        old_buffers.current_position == 0 ||
        old_buffers.previous_position == 0 ||
        old_buffers.velocity == 0 ||
        old_buffers.collision_pushout == 0 ||
        old_buffers.cloth_cloth_pushout == 0 ||
        old_buffers.contact_motion_delta == 0 ||
        old_buffers.body_triangle_id == 0 ||
        next_buffers.current_position == 0 ||
        next_buffers.previous_position == 0 ||
        next_buffers.velocity == 0 ||
        next_buffers.collision_pushout == 0 ||
        next_buffers.cloth_cloth_pushout == 0 ||
        next_buffers.contact_motion_delta == 0 ||
        next_buffers.body_triangle_id == 0) {
        return false;
    }

    const GLsizeiptr old_offset_bytes = float_byte_size(old_data.vertex_offset, gpu_position_components);
    const GLsizeiptr next_offset_bytes = float_byte_size(next_data.vertex_offset, gpu_position_components);
    const GLsizeiptr position_size_bytes = float_byte_size(next_data.vertex_count, gpu_position_components);
    const GLsizeiptr old_vec4_offset_bytes = scalar_byte_size(old_data.vertex_offset, sizeof(glm::vec4));
    const GLsizeiptr next_vec4_offset_bytes = scalar_byte_size(next_data.vertex_offset, sizeof(glm::vec4));
    const GLsizeiptr vec4_size_bytes = scalar_byte_size(next_data.vertex_count, sizeof(glm::vec4));
    const GLsizeiptr old_body_triangle_id_offset_bytes =
        scalar_byte_size(old_data.vertex_offset, sizeof(std::uint32_t));
    const GLsizeiptr next_body_triangle_id_offset_bytes =
        scalar_byte_size(next_data.vertex_offset, sizeof(std::uint32_t));
    const GLsizeiptr body_triangle_id_size_bytes =
        scalar_byte_size(next_data.vertex_count, sizeof(std::uint32_t));

    gl.glCopyNamedBufferSubData(old_buffers.current_position,
                                next_buffers.current_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.previous_position,
                                next_buffers.previous_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.velocity,
                                next_buffers.velocity,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.collision_pushout,
                                next_buffers.collision_pushout,
                                old_vec4_offset_bytes,
                                next_vec4_offset_bytes,
                                vec4_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.cloth_cloth_pushout,
                                next_buffers.cloth_cloth_pushout,
                                old_vec4_offset_bytes,
                                next_vec4_offset_bytes,
                                vec4_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.contact_motion_delta,
                                next_buffers.contact_motion_delta,
                                old_vec4_offset_bytes,
                                next_vec4_offset_bytes,
                                vec4_size_bytes);
    gl.glCopyNamedBufferSubData(old_buffers.body_triangle_id,
                                next_buffers.body_triangle_id,
                                old_body_triangle_id_offset_bytes,
                                next_body_triangle_id_offset_bytes,
                                body_triangle_id_size_bytes);
    return true;
}

bool copy_attachment_target_buffers(const GarmentBufferRanges& old_data,
                                    const GarmentBufferRanges& next_data,
                                    const std::vector<ElementRange>& active_attachment_ranges,
                                    const ClothBufferSet& old_buffers,
                                    const ClothBufferSet& next_buffers,
                                    std::uint32_t& copied_target_count,
                                    QOpenGLFunctions_4_5_Core& gl)
{
    copied_target_count = 0u;

    const auto range_iter = std::find_if(active_attachment_ranges.begin(),
                                         active_attachment_ranges.end(),
                                         [&old_data](const ElementRange& range) {
                                             return range.offset == old_data.attachment_constraint_offset;
                                         });
    if (range_iter == active_attachment_ranges.end() || range_iter->count == 0u) {
        return true;
    }
    if (range_iter->count > old_data.attachment_constraint_count ||
        range_iter->count > next_data.attachment_constraint_count ||
        old_buffers.attachment_indices == 0 ||
        old_buffers.attachment_barycentric_offset == 0 ||
        next_buffers.attachment_indices == 0 ||
        next_buffers.attachment_barycentric_offset == 0) {
        return false;
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    const GLsizeiptr old_index_offset_bytes =
        scalar_byte_size(old_data.attachment_constraint_offset, sizeof(glm::uvec2));
    const GLsizeiptr next_index_offset_bytes =
        scalar_byte_size(next_data.attachment_constraint_offset, sizeof(glm::uvec2));
    const GLsizeiptr index_size_bytes = scalar_byte_size(range_iter->count, sizeof(glm::uvec2));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_indices,
                                next_buffers.attachment_indices,
                                old_index_offset_bytes,
                                next_index_offset_bytes,
                                index_size_bytes);

    const GLsizeiptr old_barycentric_offset_bytes =
        scalar_byte_size(old_data.attachment_constraint_offset, sizeof(glm::vec4));
    const GLsizeiptr next_barycentric_offset_bytes =
        scalar_byte_size(next_data.attachment_constraint_offset, sizeof(glm::vec4));
    const GLsizeiptr barycentric_size_bytes = scalar_byte_size(range_iter->count, sizeof(glm::vec4));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_barycentric_offset,
                                next_buffers.attachment_barycentric_offset,
                                old_barycentric_offset_bytes,
                                next_barycentric_offset_bytes,
                                barycentric_size_bytes);

    copied_target_count = range_iter->count;
    return true;
}

bool rebuild_buffer_data(const std::vector<BufferRebuildGarmentData>& rebuild_garments,
                         const std::vector<GarmentBufferRanges>& old_garments,
                         const std::vector<ElementRange>& old_attachment_ranges,
                         const ClothBufferSet& old_buffer_set,
                         const ClothBufferSet& rebuild_buffer_set,
                         BufferRebuildUploadData& rebuild_upload_data,
                         std::uint32_t reset_garment_id,
                         QOpenGLFunctions_4_5_Core& gl)
{
    for (const BufferRebuildGarmentData& rebuild_garment : rebuild_garments) {
        const GarmentBufferRanges& buffer_ranges = rebuild_garment.buffer_ranges;
        const bool reset_garment = buffer_ranges.id == reset_garment_id;
        const auto old_iter = std::find_if(old_garments.begin(),
                                           old_garments.end(),
                                           [&buffer_ranges](const GarmentBufferRanges& old_data) {
                                               return old_data.id == buffer_ranges.id;
                                           });

        if (reset_garment) {
            upload_position_data(rebuild_buffer_set,
                                 rebuild_garment.garment->mesh.vertices,
                                 buffer_ranges,
                                 gl);
        } else {
            if (old_iter == old_garments.end() || !copy_dynamic_state_buffers(*old_iter,
                                                                              buffer_ranges,
                                                                              old_buffer_set,
                                                                              rebuild_buffer_set,
                                                                              gl)) {
                std::cerr << "Cannot rebuild cloth position data for garment id " << buffer_ranges.id
                          << ".\n";
                return false;
            }
        }

        std::uint32_t copied_attachment_target_count = 0u;
        if (!reset_garment && !copy_attachment_target_buffers(*old_iter,
                                                              buffer_ranges,
                                                              old_attachment_ranges,
                                                              old_buffer_set,
                                                              rebuild_buffer_set,
                                                              copied_attachment_target_count,
                                                              gl)) {
            std::cerr << "Cannot rebuild cloth attachment target data for garment id " << buffer_ranges.id
                      << ".\n";
            return false;
        }
        if (copied_attachment_target_count > 0u) {
            rebuild_upload_data.attachment_ranges.push_back(
                {buffer_ranges.attachment_constraint_offset, copied_attachment_target_count});
        }

        build_buffer_rebuild_upload_data(*rebuild_garment.garment, buffer_ranges, rebuild_upload_data);
    }

    return true;
}

std::vector<GarmentBufferRanges> make_buffer_rebuild_ranges(
    const std::vector<BufferRebuildGarmentData>& rebuild_garments)
{
    std::vector<GarmentBufferRanges> rebuild_ranges;
    rebuild_ranges.reserve(rebuild_garments.size());
    for (const BufferRebuildGarmentData& rebuild_garment : rebuild_garments) {
        rebuild_ranges.push_back(rebuild_garment.buffer_ranges);
    }
    return rebuild_ranges;
}
}

ClothGpuResources::ClothGpuResources(ClothGpuResources&& other) noexcept
{
    buffers_ = other.buffers_;
    garments_ = std::move(other.garments_);
    stretch_color_ranges_ = std::move(other.stretch_color_ranges_);
    bending_color_ranges_ = std::move(other.bending_color_ranges_);
    attachment_ranges_ = std::move(other.attachment_ranges_);
    used_elements_ = other.used_elements_;
    allocated_elements_ = other.allocated_elements_;

    base_positions_ = other.base_positions_;
    base_position_vertex_count_ = other.base_position_vertex_count_;

    other.reset_resources();
}

void ClothGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    reset_resources();
}

void ClothGpuResources::reset_resources() noexcept
{
    buffers_ = {};
    garments_.clear();
    stretch_color_ranges_.clear();
    bending_color_ranges_.clear();
    attachment_ranges_.clear();
    used_elements_ = {};
    allocated_elements_ = {};

    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}

// garment buffer updates //
bool ClothGpuResources::update_garment_buffers(const std::vector<GarmentObject>& garments,
                                               std::uint32_t reset_garment_id,
                                               QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return true;
    }

    if (reset_garment_id != 0u) {
        return rebuild_buffers(garments, reset_garment_id, gl);
    }

    // 1. GPU에만 있는 garment 발견 (scene에서 삭제된 것) -> buffer rebuild 필요
    bool needs_buffer_rebuild = false;
    for (const GarmentBufferRanges& buffer_ranges : garments_) {
        const auto iter =
            std::find_if(garments.begin(), garments.end(), [&buffer_ranges](const GarmentObject& garment) {
                return garment.id == buffer_ranges.id;
            });

        if (iter == garments.end()) {
            needs_buffer_rebuild = true;
            break;
        }
    }
    if (needs_buffer_rebuild) {
        // 남길 garments 정보만 새로운 buffer에 재배치
        return rebuild_buffers(garments, 0u, gl);
    }

    // 2. 새로운 garment 발견 (scene에서 추가된 것) -> 기존 buffer에 append
    for (const GarmentObject& garment : garments) {
        if (find_garment_buffer_ranges(garment.id) == nullptr && !append_garment(garment, gl)) {
            return false;
        }
    }
    return true;
}

// garment 삭제 -> buffer rebuild: 남은 garments 빈틈 없이 연속적으로 새 buffer에 재배치
bool ClothGpuResources::update_garment_placement(const GarmentObject& garment,
                                                 bool update_rest_lengths,
                                                 QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentBufferRanges* buffer_ranges = find_garment_buffer_ranges(garment.id);
    if (!is_initialized() || buffer_ranges == nullptr || !is_uploadable_mesh(garment.mesh)) {
        return false;
    }

    upload_position_data(buffers_, garment.mesh.vertices, *buffer_ranges, gl);
    if (update_rest_lengths) {
        upload_rest_length_data(buffers_, garment, *buffer_ranges, gl);
    }

    return true;
}

bool ClothGpuResources::upload_garment_attachment_vertices(const GarmentObject& garment,
                                                           ElementRange& target_range,
                                                           QOpenGLFunctions_4_5_Core& gl)
{
    target_range = {};
    const GarmentBufferRanges* buffer_ranges = find_garment_buffer_ranges(garment.id);
    if (!is_initialized() || buffer_ranges == nullptr || !has_valid_attachment_vertices(garment)) {
        return false;
    }

    const auto attachment_constraint_count =
        static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());
    if (attachment_constraint_count > buffer_ranges->attachment_constraint_count) {
        std::cerr << "Too many attachment vertices for garment id " << garment.id << ".\n";
        return false;
    }

    target_range = {buffer_ranges->attachment_constraint_offset, attachment_constraint_count};
    if (attachment_constraint_count == 0u) {
        set_attachment_range(buffer_ranges->attachment_constraint_offset, 0u, attachment_ranges_);
        return true;
    }

    AttachmentConstraintUploadData upload_data;
    upload_data.attachment_indices.reserve(attachment_constraint_count);
    upload_data.barycentric_offsets.reserve(attachment_constraint_count);
    build_attachment_vertex_upload_data(garment.mesh.attachment_vertex_indices, *buffer_ranges, upload_data);
    upload_attachment_constraints_data(buffers_,
                                       buffer_ranges->attachment_constraint_offset,
                                       upload_data,
                                       gl);
    return true;
}

bool ClothGpuResources::activate_attachment_targets(const ElementRange& target_range)
{
    if (!is_initialized() ||
        target_range.offset > used_elements_.attachment_constraint ||
        target_range.count > used_elements_.attachment_constraint - target_range.offset) {
        return false;
    }

    set_attachment_range(target_range.offset, target_range.count, attachment_ranges_);
    return true;
}

bool ClothGpuResources::deactivate_attachment_targets(std::uint32_t garment_id)
{
    const GarmentBufferRanges* buffer_ranges = find_garment_buffer_ranges(garment_id);
    if (!is_initialized() || buffer_ranges == nullptr) {
        return false;
    }

    set_attachment_range(buffer_ranges->attachment_constraint_offset, 0u, attachment_ranges_);
    return true;
}

bool ClothGpuResources::save_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    if (buffers_.current_position == 0 || used_elements_.vertex == 0) {
        return false;
    }

    clear_base_positions(gl);

    const GLsizeiptr position_bytes = float_byte_size(used_elements_.vertex, gpu_position_components);
    gl.glCreateBuffers(1, &base_positions_);
    gl.glNamedBufferData(base_positions_, position_bytes, nullptr, GL_DYNAMIC_COPY);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(buffers_.current_position, base_positions_, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    base_position_vertex_count_ = used_elements_.vertex;
    return true;
}

bool ClothGpuResources::restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const
{
    if (base_positions_ == 0 ||
        buffers_.current_position == 0 ||
        buffers_.previous_position == 0 ||
        buffers_.velocity == 0 ||
        buffers_.collision_pushout == 0 ||
        buffers_.cloth_cloth_pushout == 0 ||
        buffers_.contact_motion_delta == 0 ||
        used_elements_.vertex == 0 ||
        base_position_vertex_count_ != used_elements_.vertex) {
        return false;
    }

    const GLsizeiptr position_bytes = float_byte_size(used_elements_.vertex, gpu_position_components);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(base_positions_, buffers_.current_position, 0, 0, position_bytes);
    gl.glCopyNamedBufferSubData(base_positions_, buffers_.previous_position, 0, 0, position_bytes);
    clear_dynamic_state_range(buffers_, 0, used_elements_.vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    return true;
}

void ClothGpuResources::clear_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &base_positions_);

    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}

bool ClothGpuResources::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                        std::uint32_t reset_garment_id,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<BufferRebuildGarmentData> rebuild_garments;
    ClothBufferElementCounts rebuild_element_counts;

    // 1. 새로운 GPU buffer에서 garment들이 사용할 구간 배정 (빈틈 없이 연속적으로)
    if (!assign_buffer_rebuild_ranges(garments, rebuild_garments, rebuild_element_counts)) {
        return false;
    }

    const ClothBufferSet old_buffer_set = buffers_;
    const std::vector<GarmentBufferRanges>& old_garments = garments_;
    const std::vector<ElementRange>& old_attachment_ranges = attachment_ranges_;

    // 2. 새롭게 할당할 GPU buffer 생성 & 새 buffer에 upload할 데이터를 담을 임시 container 준비
    ClothBufferSet rebuild_buffer_set = create_buffer_set(rebuild_element_counts, gl);
    BufferRebuildUploadData rebuild_upload_data =
        prepare_buffer_rebuild_upload_data(rebuild_element_counts, rebuild_garments.size());

    if (!rebuild_buffer_data(rebuild_garments,
                             old_garments,
                             old_attachment_ranges,
                             old_buffer_set,
                             rebuild_buffer_set,
                             rebuild_upload_data,
                             reset_garment_id,
                             gl)) {
        delete_buffer_set(rebuild_buffer_set, gl);
        return false;
    }

    // 준비한 data 값을 새 buffer에 upload
    upload_topology_data(rebuild_buffer_set, TopologyUploadOffsets{}, rebuild_upload_data.topology, gl);
    upload_stretch_constraints_data(rebuild_buffer_set, 0, rebuild_upload_data.stretch_constraints, gl);
    upload_bending_constraints_data(rebuild_buffer_set, 0, rebuild_upload_data.bending_constraints, gl);

    replace_with_rebuild_buffers(rebuild_buffer_set,
                                 make_buffer_rebuild_ranges(rebuild_garments),
                                 std::move(rebuild_upload_data.stretch_color_ranges),
                                 std::move(rebuild_upload_data.bending_color_ranges),
                                 std::move(rebuild_upload_data.attachment_ranges),
                                 rebuild_element_counts,
                                 gl);
    return true;
}

void ClothGpuResources::replace_with_rebuild_buffers(ClothBufferSet rebuild_buffer_set,
                                                     std::vector<GarmentBufferRanges> rebuild_ranges,
                                                     std::vector<ElementRange> rebuild_stretch_color_ranges,
                                                     std::vector<ElementRange> rebuild_bending_color_ranges,
                                                     std::vector<ElementRange> rebuild_attachment_ranges,
                                                     const ClothBufferElementCounts& rebuild_element_counts,
                                                     QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    buffers_ = rebuild_buffer_set;
    garments_ = std::move(rebuild_ranges);
    stretch_color_ranges_ = std::move(rebuild_stretch_color_ranges);
    bending_color_ranges_ = std::move(rebuild_bending_color_ranges);
    attachment_ranges_ = std::move(rebuild_attachment_ranges);
    used_elements_ = rebuild_element_counts;
    allocated_elements_ = rebuild_element_counts;

    configure_vao(gl);
}

// 새 garment 추가
bool ClothGpuResources::append_garment(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    if (!is_uploadable_mesh(garment.mesh)) {
        std::cerr << "Cannot upload invalid garment mesh for garment id " << garment.id << ".\n";
        return false;
    }

    // 이미 upload된 garment면 기존 GPU buffer 사용
    if (find_garment_buffer_ranges(garment.id) != nullptr) {
        return true;
    }

    const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
    const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;
    const auto attachment_constraint_count =
        static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());
    GarmentBufferRanges buffer_ranges =
        make_garment_buffer_ranges(garment,
                                   used_elements_,
                                   static_cast<std::uint32_t>(stretch_constraints.colorized_edges.size()),
                                   static_cast<std::uint32_t>(bending_constraints.colorized_edges.size()),
                                   attachment_constraint_count);
    const ClothBufferElementCounts next_used_elements =
        make_next_used_elements(used_elements_, buffer_ranges);

    // 1. 현재 GPU buffer에 새 garment가 사용할 구간이 남아있는지 확인 -> 부족하면 새 buffer 할당
    ensure_capacity(next_used_elements, gl);

    // 2. buffer에 garment data upload
    upload_position_data(buffers_, garment.mesh.vertices, buffer_ranges, gl);

    GarmentUploadData upload_data = prepare_garment_upload_data(garment, buffer_ranges);
    build_garment_upload_data(garment, buffer_ranges, upload_data);

    upload_topology_data(buffers_,
                         TopologyUploadOffsets{
                             buffer_ranges.index_offset,
                             buffer_ranges.vertex_offset,
                             buffer_ranges.adjacency_entry_offset,
                         },
                         upload_data.topology,
                         gl);
    upload_stretch_constraints_data(buffers_,
                                    buffer_ranges.stretch_constraint_offset,
                                    upload_data.stretch_constraints,
                                    gl);
    upload_bending_constraints_data(buffers_,
                                    buffer_ranges.bending_constraint_offset,
                                    upload_data.bending_constraints,
                                    gl);
    upload_attachment_constraints_data(buffers_,
                                       buffer_ranges.attachment_constraint_offset,
                                       upload_data.attachment_constraints,
                                       gl);

    // 3. garment GPU data 등록, buffer 사용량 update
    garments_.push_back(buffer_ranges);
    append_color_ranges(stretch_constraints.color_ranges,
                        buffer_ranges.stretch_constraint_offset,
                        stretch_color_ranges_);
    append_color_ranges(bending_constraints.color_ranges,
                        buffer_ranges.bending_constraint_offset,
                        bending_color_ranges_);
    used_elements_ = next_used_elements;
    return true;
}

// buffer object management //
bool ClothGpuResources::has_enough_capacity(const ClothBufferElementCounts& required_elements) const
{
    return required_elements.vertex <= allocated_elements_.vertex &&
           required_elements.index <= allocated_elements_.index &&
           required_elements.triangle <= allocated_elements_.triangle &&
           required_elements.adjacency_entry <= allocated_elements_.adjacency_entry &&
           required_elements.stretch_constraint <= allocated_elements_.stretch_constraint &&
           required_elements.bending_constraint <= allocated_elements_.bending_constraint &&
           required_elements.attachment_constraint <= allocated_elements_.attachment_constraint;
}
// GPU buffer 공간이 충분한지 확인 -> 부족하면 더 큰 buffer로 교체
void ClothGpuResources::ensure_capacity(const ClothBufferElementCounts& required_elements,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    if (has_enough_capacity(required_elements)) {
        return;
    }

    const ClothBufferElementCounts next_allocated_elements =
        make_expanded_capacity(allocated_elements_, required_elements);
    ClothBufferSet old_buffers = buffers_;
    create_buffers(next_allocated_elements, gl);
    copy_used_buffer_data(old_buffers, buffers_, used_elements_, gl);
    delete_buffer_set(old_buffers, gl);
}

void ClothGpuResources::create_buffers(const ClothBufferElementCounts& allocated_elements,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    buffers_ = create_buffer_set(allocated_elements, gl);
    allocated_elements_ = allocated_elements;

    configure_vao(gl);
}

void ClothGpuResources::delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers.vertex_normal);
    gl.glDeleteBuffers(1, &buffers.triangle_normal);
    gl.glDeleteBuffers(1, &buffers.adjacent_triangle_indices);
    gl.glDeleteBuffers(1, &buffers.adjacent_triangle_offsets);
    gl.glDeleteBuffers(1, &buffers.stretch_rest_length);
    gl.glDeleteBuffers(1, &buffers.stretch_edge_index);
    gl.glDeleteBuffers(1, &buffers.bending_rest_length);
    gl.glDeleteBuffers(1, &buffers.bending_edge_index);
    gl.glDeleteBuffers(1, &buffers.attachment_barycentric_offset);
    gl.glDeleteBuffers(1, &buffers.attachment_indices);
    gl.glDeleteBuffers(1, &buffers.index);
    gl.glDeleteBuffers(1, &buffers.previous_position);
    gl.glDeleteBuffers(1, &buffers.velocity);
    gl.glDeleteBuffers(1, &buffers.collision_pushout);
    gl.glDeleteBuffers(1, &buffers.cloth_cloth_pushout);
    gl.glDeleteBuffers(1, &buffers.contact_motion_delta);
    gl.glDeleteBuffers(1, &buffers.body_triangle_id);
    gl.glDeleteBuffers(1, &buffers.current_position);
    gl.glDeleteVertexArrays(1, &buffers.vao);

    buffers = {};
}

void ClothGpuResources::delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(buffers_, gl);
    clear_base_positions(gl);
}

void ClothGpuResources::configure_vao(QOpenGLFunctions_4_5_Core& gl)
{
    constexpr GLuint position_attribute_location = 0;
    constexpr GLuint position_binding_index = 0;
    constexpr GLuint position_relative_offset = 0;

    gl.glVertexArrayVertexBuffer(buffers_.vao,
                                 position_binding_index,
                                 buffers_.current_position,
                                 0,
                                 gpu_position_components * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(buffers_.vao, position_attribute_location);
    gl.glVertexArrayAttribFormat(buffers_.vao,
                                 position_attribute_location,
                                 3,
                                 GL_FLOAT,
                                 GL_FALSE,
                                 position_relative_offset);
    gl.glVertexArrayAttribBinding(buffers_.vao, position_attribute_location, position_binding_index);
    gl.glVertexArrayElementBuffer(buffers_.vao, buffers_.index);
}

// rendering //
void ClothGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.vertex_normal == 0) {
        return;
    }

    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

void ClothGpuResources::draw_garment(std::uint32_t garment_id, QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferRanges* buffer_ranges = find_garment_buffer_ranges(garment_id);
    if (!is_initialized() || buffer_ranges == nullptr || buffer_ranges->index_count == 0) {
        return;
    }

    const auto index_offset_bytes =
        static_cast<std::uintptr_t>(buffer_ranges->index_offset) * sizeof(std::uint32_t);
    gl.glBindVertexArray(buffers_.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(buffer_ranges->index_count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// state checks //
const GarmentBufferRanges* ClothGpuResources::find_garment_buffer_ranges(std::uint32_t garment_id) const
{
    const auto iter = std::find_if(
        garments_.begin(),
        garments_.end(),
        [garment_id](const GarmentBufferRanges& buffer_ranges) { return buffer_ranges.id == garment_id; });
    if (iter == garments_.end()) {
        return nullptr;
    }

    return &(*iter);
}

bool ClothGpuResources::has_gpu_objects() const
{
    return buffers_.vao != 0 &&
           buffers_.current_position != 0 &&
           buffers_.previous_position != 0 &&
           buffers_.velocity != 0 &&
           buffers_.collision_pushout != 0 &&
           buffers_.cloth_cloth_pushout != 0 &&
           buffers_.contact_motion_delta != 0 &&
           buffers_.body_triangle_id != 0 &&
           buffers_.index != 0 &&
           buffers_.adjacent_triangle_offsets != 0 &&
           buffers_.adjacent_triangle_indices != 0 &&
           buffers_.stretch_edge_index != 0 &&
           buffers_.stretch_rest_length != 0 &&
           buffers_.bending_edge_index != 0 &&
           buffers_.bending_rest_length != 0 &&
           buffers_.attachment_indices != 0 &&
           buffers_.attachment_barycentric_offset != 0 &&
           buffers_.triangle_normal != 0 &&
           buffers_.vertex_normal != 0;
}

bool ClothGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           !garments_.empty() &&
           used_elements_.vertex > 0 &&
           used_elements_.index > 0 &&
           used_elements_.triangle > 0 &&
           used_elements_.stretch_constraint > 0 &&
           used_elements_.bending_constraint > 0 &&
           !stretch_color_ranges_.empty() &&
           !bending_color_ranges_.empty();
}

// getter //

const std::vector<GarmentBufferRanges>& ClothGpuResources::garment_buffer_ranges() const
{
    return garments_;
}

ClothMotionBufferView ClothGpuResources::motion_buffer_view() const
{
    ClothMotionBufferView view;
    view.current_position_buffer = buffers_.current_position;
    view.previous_position_buffer = buffers_.previous_position;
    view.velocity_buffer = buffers_.velocity;
    view.vertex_count = used_elements_.vertex;
    return view;
}

ClothCollisionPushoutBufferView ClothGpuResources::collision_pushout_buffer_view() const
{
    ClothCollisionPushoutBufferView view;
    view.collision_pushout_buffer = buffers_.collision_pushout;
    view.cloth_cloth_pushout_buffer = buffers_.cloth_cloth_pushout;
    view.vertex_count = used_elements_.vertex;
    return view;
}

ClothContactMotionBufferView ClothGpuResources::contact_motion_buffer_view() const
{
    ClothContactMotionBufferView view;
    view.contact_motion_delta_buffer = buffers_.contact_motion_delta;
    view.vertex_count = used_elements_.vertex;
    return view;
}

ClothBodyTriangleIdBufferView ClothGpuResources::body_triangle_id_buffer_view() const
{
    ClothBodyTriangleIdBufferView view;
    view.body_triangle_id_buffer = buffers_.body_triangle_id;
    view.vertex_count = used_elements_.vertex;
    return view;
}

void ClothGpuResources::copy_current_positions_to_previous(QOpenGLFunctions_4_5_Core& gl) const
{
    if (buffers_.current_position == 0 ||
        buffers_.previous_position == 0 ||
        buffers_.velocity == 0 ||
        buffers_.collision_pushout == 0 ||
        buffers_.cloth_cloth_pushout == 0 ||
        buffers_.contact_motion_delta == 0 ||
        used_elements_.vertex == 0) {
        return;
    }

    const GLsizeiptr position_bytes = float_byte_size(used_elements_.vertex, gpu_position_components);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(buffers_.current_position, buffers_.previous_position, 0, 0, position_bytes);
    clear_dynamic_state_range(buffers_, 0, used_elements_.vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

DistanceConstraintBufferView ClothGpuResources::stretch_constraint_buffer_view() const
{
    DistanceConstraintBufferView view;
    view.edge_index_buffer = buffers_.stretch_edge_index;
    view.rest_length_buffer = buffers_.stretch_rest_length;
    view.constraint_count = used_elements_.stretch_constraint;
    view.color_ranges = &stretch_color_ranges_;
    return view;
}

DistanceConstraintBufferView ClothGpuResources::bending_constraint_buffer_view() const
{
    DistanceConstraintBufferView view;
    view.edge_index_buffer = buffers_.bending_edge_index;
    view.rest_length_buffer = buffers_.bending_rest_length;
    view.constraint_count = used_elements_.bending_constraint;
    view.color_ranges = &bending_color_ranges_;
    return view;
}

AttachmentConstraintBufferView ClothGpuResources::attachment_constraint_buffer_view() const
{
    AttachmentConstraintBufferView view;
    view.attachment_index_buffer = buffers_.attachment_indices;
    view.barycentric_offset_buffer = buffers_.attachment_barycentric_offset;
    view.constraint_count = used_elements_.attachment_constraint;
    view.ranges = &attachment_ranges_;
    return view;
}

ClothMeshTopologyResources ClothGpuResources::mesh_topology_resources() const
{
    ClothMeshTopologyResources topology;
    topology.position_buffer = buffers_.current_position;
    topology.triangle_index_buffer = buffers_.index;
    topology.adjacent_triangle_offsets_buffer = buffers_.adjacent_triangle_offsets;
    topology.adjacent_triangle_indices_buffer = buffers_.adjacent_triangle_indices;
    topology.vertex_count = used_elements_.vertex;
    topology.triangle_count = used_elements_.triangle;
    return topology;
}

ClothNormalResources ClothGpuResources::mesh_normal_resources() const
{
    ClothNormalResources normals;
    normals.triangle_normal_buffer = buffers_.triangle_normal;
    normals.vertex_normal_buffer = buffers_.vertex_normal;
    return normals;
}
