#include "gpu/cloth/ClothGpuResources.h"

#include "gpu/scene/MeshBufferResources.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t position_components = 3;
constexpr std::uint32_t normal_components = 4;

// size / capacity //
GLsizeiptr byte_size(std::uint32_t count, std::uint32_t component_count, std::size_t component_size)
{
    return static_cast<GLsizeiptr>(
        static_cast<std::size_t>(count) * static_cast<std::size_t>(component_count) * component_size
    );
}

GLsizeiptr scalar_byte_size(std::uint32_t count, std::size_t component_size)
{
    return static_cast<GLsizeiptr>(static_cast<std::size_t>(count) * component_size);
}

ClothBufferElementCounts make_expanded_capacity(const ClothBufferElementCounts& allocated_elements,
                                                const ClothBufferElementCounts& required_elements)
{
    const auto expand_count = [](std::uint32_t current_capacity, std::uint32_t required_capacity) {
        if (required_capacity <= current_capacity) {
            return current_capacity;
        }
        if (current_capacity == 0) {
            return required_capacity;
        }

        const std::uint32_t doubled_capacity = current_capacity > (std::numeric_limits<std::uint32_t>::max() / 2u)
            ? std::numeric_limits<std::uint32_t>::max()
            : current_capacity * 2u;
        return std::max(required_capacity, doubled_capacity);
    };

    ClothBufferElementCounts next_allocated_elements;
    next_allocated_elements.vertex = expand_count(allocated_elements.vertex, required_elements.vertex);
    next_allocated_elements.index = expand_count(allocated_elements.index, required_elements.index);
    next_allocated_elements.triangle = expand_count(allocated_elements.triangle, required_elements.triangle);
    next_allocated_elements.adjacency_entry = expand_count(allocated_elements.adjacency_entry,
                                                           required_elements.adjacency_entry);
    next_allocated_elements.stretch_constraint = expand_count(allocated_elements.stretch_constraint,
                                                              required_elements.stretch_constraint);
    next_allocated_elements.bending_constraint = expand_count(allocated_elements.bending_constraint,
                                                              required_elements.bending_constraint);
    return next_allocated_elements;
}

// state checks //
bool is_uploadable_mesh(const GarmentMesh& garment_mesh)
{
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components);
    return !garment_mesh.vertices.empty() &&
           !garment_mesh.indices.empty() &&
           garment_mesh.vertices.size() % position_components  == 0u &&
           garment_mesh.indices.size() % 3u == 0u &&
           garment_mesh.adjacency.is_valid(vertex_count) &&
           garment_mesh.stretch_constraints.is_valid() &&
           garment_mesh.bending_constraints.is_valid();
}

// garment range //
struct BufferRebuildGarmentData final {
    const GarmentObject* garment = nullptr;
    GarmentBufferRanges buffer_ranges;
};

GarmentBufferRanges make_garment_buffer_ranges(const GarmentObject& garment,
                                               const ClothBufferElementCounts& offsets,
                                               std::uint32_t stretch_constraint_count,
                                               std::uint32_t bending_constraint_count)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    GarmentBufferRanges buffer_ranges;
    buffer_ranges.id = garment.id;
    buffer_ranges.vertex_offset = offsets.vertex;
    buffer_ranges.vertex_count = static_cast<std::uint32_t>(garment.mesh.vertices.size() / position_components);
    buffer_ranges.index_offset = offsets.index;
    buffer_ranges.index_count = static_cast<std::uint32_t>(garment.mesh.indices.size());
    buffer_ranges.triangle_offset = offsets.triangle;
    buffer_ranges.triangle_count = adjacency.face_count;
    buffer_ranges.adjacency_entry_offset = offsets.adjacency_entry;
    buffer_ranges.adjacency_entry_count = static_cast<std::uint32_t>(adjacency.face_indices.size());
    buffer_ranges.stretch_constraint_offset = offsets.stretch_constraint;
    buffer_ranges.stretch_constraint_count = stretch_constraint_count;
    buffer_ranges.bending_constraint_offset = offsets.bending_constraint;
    buffer_ranges.bending_constraint_count = bending_constraint_count;
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

        BufferRebuildGarmentData rebuild_garment;
        rebuild_garment.garment = &garment;
        rebuild_garment.buffer_ranges = make_garment_buffer_ranges(
            garment,
            rebuild_element_counts,
            static_cast<std::uint32_t>(stretch_constraints.colorized_edges.size()),
            static_cast<std::uint32_t>(bending_constraints.colorized_edges.size())
        );

        rebuild_element_counts.vertex += rebuild_garment.buffer_ranges.vertex_count;
        rebuild_element_counts.index += rebuild_garment.buffer_ranges.index_count;
        rebuild_element_counts.triangle += rebuild_garment.buffer_ranges.triangle_count;
        rebuild_element_counts.adjacency_entry += rebuild_garment.buffer_ranges.adjacency_entry_count;
        rebuild_element_counts.stretch_constraint += rebuild_garment.buffer_ranges.stretch_constraint_count;
        rebuild_element_counts.bending_constraint += rebuild_garment.buffer_ranges.bending_constraint_count;

        rebuild_garments.push_back(std::move(rebuild_garment));
    }

    return !rebuild_garments.empty();
}

// topology upload //
struct TopologyUploadData final {
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint32_t> adjacency_offsets;
    std::vector<std::uint32_t> adjacency_triangles;
};

struct TopologyUploadOffsets final {
    std::uint32_t index = 0;
    std::uint32_t adjacency_offset = 0;
    std::uint32_t adjacency_triangle = 0;
};

// buffer에 upload할 topology data 생성
// garment 별로 있는 정보를 하나의 buffer에 upload하기 위해 모으는 과정
void build_topology_upload_data(const GarmentObject& garment,
                                const GarmentBufferRanges& buffer_ranges,
                                std::uint32_t adjacency_offset_destination,
                                TopologyUploadData& data)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    // vertex index: 각 garment triangle의 vertex index
    for (std::uint32_t local_index : garment.mesh.indices) {
        data.vertex_indices.push_back(buffer_ranges.vertex_offset + local_index);
    }

    // adjacency_offsets: 각 vertex의 adjacency triangle 배열에서 시작 위치
    for (std::uint32_t local_vertex = 0; local_vertex <= buffer_ranges.vertex_count; ++local_vertex) {
        const std::uint32_t destination_vertex = adjacency_offset_destination + local_vertex;
        data.adjacency_offsets[destination_vertex] = buffer_ranges.adjacency_entry_offset + adjacency.offsets[local_vertex];
    }

    // adjacency_triangles: 각 vertex가 속한 triangle index 배열
    for (std::uint32_t local_face : adjacency.face_indices) {
        data.adjacency_triangles.push_back(buffer_ranges.triangle_offset + local_face);
    }
}

// topology data를 GPU buffer에 upload
void upload_topology_data(const ClothBufferSet& buffers,
                          const TopologyUploadOffsets& offsets,
                          const TopologyUploadData& data,
                          QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(buffers.index,
                            scalar_byte_size(offsets.index, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.vertex_indices.size()), sizeof(std::uint32_t)),
                            data.vertex_indices.data());
    gl.glNamedBufferSubData(buffers.adjacency_offset,
                            scalar_byte_size(offsets.adjacency_offset, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.adjacency_offsets.size()), sizeof(std::uint32_t)),
                            data.adjacency_offsets.data());
    gl.glNamedBufferSubData(buffers.adjacency_triangle,
                            scalar_byte_size(offsets.adjacency_triangle, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.adjacency_triangles.size()), sizeof(std::uint32_t)),
                            data.adjacency_triangles.data());
}

// stretch constraint //
struct StretchConstraintUploadData final {
    std::vector<std::uint32_t> edge_indices;
    std::vector<float> rest_lengths;
};

struct BendingConstraintUploadData final {
    std::vector<std::uint32_t> edge_indices;
    std::vector<float> rest_lengths;
};

void build_stretch_constraint_upload_data(const GarmentDistanceConstraints& stretch_constraints,
                                          const GarmentBufferRanges& buffer_ranges,
                                          StretchConstraintUploadData& data)
{
    for (const MeshEdge& edge : stretch_constraints.colorized_edges) {
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_a);
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_b);
    }

    data.rest_lengths.insert(data.rest_lengths.end(),
                             stretch_constraints.rest_lengths.begin(),
                             stretch_constraints.rest_lengths.end());
}

void build_bending_constraint_upload_data(const GarmentDistanceConstraints& bending_constraints,
                                          const GarmentBufferRanges& buffer_ranges,
                                          BendingConstraintUploadData& data)
{
    for (const MeshEdge& edge : bending_constraints.colorized_edges) {
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_a);
        data.edge_indices.push_back(buffer_ranges.vertex_offset + edge.vertex_b);
    }

    data.rest_lengths.insert(data.rest_lengths.end(),
                             bending_constraints.rest_lengths.begin(),
                             bending_constraints.rest_lengths.end());
}

// stretch_constraints를 GPU buffer에 upload
void upload_stretch_constraints_data(const ClothBufferSet& buffers,
                                     std::uint32_t constraint_offset,
                                     const StretchConstraintUploadData& data,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(buffers.stretch_edge_index,
                            scalar_byte_size(constraint_offset * 2u, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.edge_indices.size()), sizeof(std::uint32_t)),
                            data.edge_indices.data());

    gl.glNamedBufferSubData(buffers.stretch_rest_length,
                            scalar_byte_size(constraint_offset, sizeof(float)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.rest_lengths.size()), sizeof(float)),
                            data.rest_lengths.data());
}

void upload_bending_constraints_data(const ClothBufferSet& buffers,
                                     std::uint32_t constraint_offset,
                                     const BendingConstraintUploadData& data,
                                     QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(buffers.bending_edge_index,
                            scalar_byte_size(constraint_offset * 2u, sizeof(std::uint32_t)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.edge_indices.size()), sizeof(std::uint32_t)),
                            data.edge_indices.data());

    gl.glNamedBufferSubData(buffers.bending_rest_length,
                            scalar_byte_size(constraint_offset, sizeof(float)),
                            scalar_byte_size(static_cast<std::uint32_t>(data.rest_lengths.size()), sizeof(float)),
                            data.rest_lengths.data());
}

void append_color_ranges(const std::vector<MeshEdgeRange>& local_ranges,
                         std::uint32_t constraint_offset,
                         std::vector<ConstraintRange>& color_ranges)
{
    for (const MeshEdgeRange& local_range : local_ranges) {
        color_ranges.push_back({constraint_offset + local_range.offset, local_range.count});
    }
}

// garment upload //
struct GarmentUploadData final {
    TopologyUploadData topology;
    StretchConstraintUploadData stretch_constraints;
    BendingConstraintUploadData bending_constraints;
};

GarmentUploadData prepare_garment_upload_data(const GarmentObject& garment,
                                              const GarmentBufferRanges& buffer_ranges)
{
    GarmentUploadData data;
    data.topology.vertex_indices.reserve(garment.mesh.indices.size());
    data.topology.adjacency_offsets.resize(static_cast<std::size_t>(buffer_ranges.vertex_count) + 1u, 0);
    data.topology.adjacency_triangles.reserve(garment.mesh.adjacency.face_indices.size());
    data.stretch_constraints.edge_indices.reserve(buffer_ranges.stretch_constraint_count * 2u);
    data.stretch_constraints.rest_lengths.reserve(buffer_ranges.stretch_constraint_count);
    data.bending_constraints.edge_indices.reserve(buffer_ranges.bending_constraint_count * 2u);
    data.bending_constraints.rest_lengths.reserve(buffer_ranges.bending_constraint_count);
    return data;
}

void build_garment_upload_data(const GarmentObject& garment,
                               const GarmentBufferRanges& buffer_ranges,
                               GarmentUploadData& data)
{
    build_topology_upload_data(garment, buffer_ranges, 0, data.topology);
    build_stretch_constraint_upload_data(garment.mesh.stretch_constraints, buffer_ranges, data.stretch_constraints);
    build_bending_constraint_upload_data(garment.mesh.bending_constraints, buffer_ranges, data.bending_constraints);
}

void upload_position_data(const ClothBufferSet& buffers,
                          const std::vector<float>& vertices,
                          const GarmentBufferRanges& buffer_ranges,
                          QOpenGLFunctions_4_5_Core& gl)
{
    const GLsizeiptr position_offset_bytes = byte_size(buffer_ranges.vertex_offset, position_components, sizeof(float));
    const GLsizeiptr position_size_bytes = byte_size(buffer_ranges.vertex_count, position_components, sizeof(float));

    gl.glNamedBufferSubData(buffers.rest_position, position_offset_bytes, position_size_bytes, vertices.data());
    gl.glNamedBufferSubData(buffers.current_position, position_offset_bytes, position_size_bytes, vertices.data());
    gl.glNamedBufferSubData(buffers.previous_position, position_offset_bytes, position_size_bytes, vertices.data());
}

// buffer rebuild upload //
struct BufferRebuildUploadData final {
    TopologyUploadData topology;
    StretchConstraintUploadData stretch_constraints;
    BendingConstraintUploadData bending_constraints;
    std::vector<ConstraintRange> stretch_color_ranges;
    std::vector<ConstraintRange> bending_color_ranges;
};

BufferRebuildUploadData prepare_buffer_rebuild_upload_data(const ClothBufferElementCounts& element_counts, std::size_t garment_count)
{
    BufferRebuildUploadData data;
    data.topology.vertex_indices.reserve(element_counts.index);
    data.topology.adjacency_offsets.resize(static_cast<std::size_t>(element_counts.vertex) + 1u, 0);
    data.topology.adjacency_triangles.reserve(element_counts.adjacency_entry);
    data.stretch_constraints.edge_indices.reserve(element_counts.stretch_constraint * 2u);
    data.stretch_constraints.rest_lengths.reserve(element_counts.stretch_constraint);
    data.bending_constraints.edge_indices.reserve(element_counts.bending_constraint * 2u);
    data.bending_constraints.rest_lengths.reserve(element_counts.bending_constraint);
    data.stretch_color_ranges.reserve(garment_count * 8u);
    data.bending_color_ranges.reserve(garment_count * 8u);
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
ClothBufferSet create_buffer_set(const ClothBufferElementCounts& allocated_elements, QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet buffers;
    gl.glCreateVertexArrays(1, &buffers.vao);
    gl.glCreateBuffers(1, &buffers.rest_position);
    gl.glCreateBuffers(1, &buffers.current_position);
    gl.glCreateBuffers(1, &buffers.previous_position);
    gl.glCreateBuffers(1, &buffers.index);
    gl.glCreateBuffers(1, &buffers.adjacency_offset);
    gl.glCreateBuffers(1, &buffers.adjacency_triangle);
    gl.glCreateBuffers(1, &buffers.stretch_edge_index);
    gl.glCreateBuffers(1, &buffers.stretch_rest_length);
    gl.glCreateBuffers(1, &buffers.bending_edge_index);
    gl.glCreateBuffers(1, &buffers.bending_rest_length);
    gl.glCreateBuffers(1, &buffers.triangle_normal);
    gl.glCreateBuffers(1, &buffers.vertex_normal);

    gl.glNamedBufferData(buffers.rest_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.current_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.previous_position,
                         byte_size(allocated_elements.vertex, position_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.index,
                         scalar_byte_size(allocated_elements.index, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacency_offset,
                         scalar_byte_size(allocated_elements.vertex + 1u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacency_triangle,
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
    gl.glNamedBufferData(buffers.triangle_normal,
                         byte_size(allocated_elements.triangle, normal_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(buffers.vertex_normal,
                         byte_size(allocated_elements.vertex, normal_components , sizeof(float)),
                         nullptr,
                         GL_DYNAMIC_DRAW);

    return buffers;
}

void copy_used_buffer_data(const ClothBufferSet& old_buffers,
                           const ClothBufferSet& next_buffers,
                           const ClothBufferElementCounts& used_elements,
                           QOpenGLFunctions_4_5_Core& gl)
{
    if (old_buffers.rest_position == 0) {
        return;
    }

    const GLsizeiptr position_bytes = byte_size(used_elements.vertex, position_components, sizeof(float));
    const GLsizeiptr index_bytes = scalar_byte_size(used_elements.index, sizeof(std::uint32_t));
    const GLsizeiptr adjacency_offset_bytes = scalar_byte_size(used_elements.vertex + 1u, sizeof(std::uint32_t));
    const GLsizeiptr adjacency_triangle_bytes = scalar_byte_size(used_elements.adjacency_entry, sizeof(std::uint32_t));
    const GLsizeiptr stretch_edge_index_bytes = scalar_byte_size(used_elements.stretch_constraint * 2u, sizeof(std::uint32_t));
    const GLsizeiptr stretch_rest_length_bytes = scalar_byte_size(used_elements.stretch_constraint, sizeof(float));
    const GLsizeiptr bending_edge_index_bytes = scalar_byte_size(used_elements.bending_constraint * 2u, sizeof(std::uint32_t));
    const GLsizeiptr bending_rest_length_bytes = scalar_byte_size(used_elements.bending_constraint, sizeof(float));

    if (position_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.rest_position, next_buffers.rest_position, 0, 0, position_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.current_position, next_buffers.current_position, 0, 0, position_bytes);
        gl.glCopyNamedBufferSubData(old_buffers.previous_position, next_buffers.previous_position, 0, 0, position_bytes);
    }
    if (index_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.index, next_buffers.index, 0, 0, index_bytes);
    }
    if (adjacency_offset_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.adjacency_offset, next_buffers.adjacency_offset, 0, 0, adjacency_offset_bytes);
    }
    if (adjacency_triangle_bytes > 0) {
        gl.glCopyNamedBufferSubData(old_buffers.adjacency_triangle,
                                    next_buffers.adjacency_triangle,
                                    0,
                                    0,
                                    adjacency_triangle_bytes);
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
}

bool copy_position_buffers(const GarmentBufferRanges& old_data,
                           const GarmentBufferRanges& next_data,
                           const ClothBufferSet& old_buffers,
                           const ClothBufferSet& next_buffers,
                           QOpenGLFunctions_4_5_Core& gl)
{
    if (old_data.vertex_count != next_data.vertex_count ||
        old_buffers.rest_position == 0 ||
        old_buffers.current_position == 0 ||
        old_buffers.previous_position == 0 ||
        next_buffers.rest_position == 0 ||
        next_buffers.current_position == 0 ||
        next_buffers.previous_position == 0) {
        return false;
    }

    const GLsizeiptr old_offset_bytes = byte_size(old_data.vertex_offset, position_components, sizeof(float));
    const GLsizeiptr next_offset_bytes = byte_size(next_data.vertex_offset, position_components, sizeof(float));
    const GLsizeiptr position_size_bytes = byte_size(next_data.vertex_count, position_components, sizeof(float));

    gl.glCopyNamedBufferSubData(old_buffers.rest_position,
                                next_buffers.rest_position,
                                old_offset_bytes,
                                next_offset_bytes,
                                position_size_bytes);
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
    return true;
}

bool rebuild_buffer_data(const std::vector<BufferRebuildGarmentData>& rebuild_garments,
                         const std::vector<GarmentBufferRanges>& old_garments,
                         const ClothBufferSet& old_buffer_set,
                         const ClothBufferSet& rebuild_buffer_set,
                         BufferRebuildUploadData& rebuild_upload_data,
                         QOpenGLFunctions_4_5_Core& gl)
{
    for (const BufferRebuildGarmentData& rebuild_garment : rebuild_garments) {
        const GarmentBufferRanges& buffer_ranges = rebuild_garment.buffer_ranges;
        const auto old_iter = std::find_if(old_garments.begin(), old_garments.end(),
            [&buffer_ranges](const GarmentBufferRanges& old_data) {
                return old_data.id == buffer_ranges.id;
            }
        );

        if (old_iter == old_garments.end() ||
            !copy_position_buffers(*old_iter, buffer_ranges, old_buffer_set, rebuild_buffer_set, gl)) {
            std::cerr << "Cannot rebuild garment position buffers for garment id " << buffer_ranges.id << ".\n";
            return false;
        }

        build_buffer_rebuild_upload_data(*rebuild_garment.garment, buffer_ranges, rebuild_upload_data);
    }

    return true;
}

std::vector<GarmentBufferRanges> make_buffer_rebuild_ranges(const std::vector<BufferRebuildGarmentData>& rebuild_garments)
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
    used_elements_ = other.used_elements_;
    allocated_elements_ = other.allocated_elements_;

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
    used_elements_ = {};
    allocated_elements_ = {};
}

// garment buffer updates //
void ClothGpuResources::update_garment_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return;
    }

    // 1. GPU에만 있는 garment 발견 (scene에서 삭제된 것) -> buffer rebuild 필요
    bool needs_buffer_rebuild = false;
    for (const GarmentBufferRanges& buffer_ranges : garments_) {
        const auto iter = std::find_if(garments.begin(), garments.end(),
            [&buffer_ranges](const GarmentObject& garment) {
                return garment.id == buffer_ranges.id;
            }
        );

        if (iter == garments.end()) {
            needs_buffer_rebuild = true;
            break;
        }
    }
    if (needs_buffer_rebuild) {
        //남길 garments 정보만 새로운 buffer에 재배치
        rebuild_garment_buffers(garments, gl);
        return;
    }

    // 2. 새로운 garment 발견 (scene에서 추가된 것) -> 기존 buffer에 append
    for (const GarmentObject& garment : garments) {
        if (find_garment_buffer_ranges(garment.id) == nullptr) {
            append_garment(garment, gl);
        }
    }
}

// garment 삭제 -> buffer rebuild: 남은 garments 빈틈 없이 연속적으로 새 buffer에 재배치
void ClothGpuResources::rebuild_garment_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<BufferRebuildGarmentData> rebuild_garments;
    ClothBufferElementCounts rebuild_element_counts;

    // 1. 새로운 GPU buffer에서 garment들이 사용할 구간 배정 (빈틈 없이 연속적으로)
    if (!assign_buffer_rebuild_ranges(garments, rebuild_garments, rebuild_element_counts)) {
        release(gl);
        return;
    }

    const ClothBufferSet old_buffer_set = buffers_;
    const std::vector<GarmentBufferRanges>& old_garments = garments_;

    // 2. 새롭게 할당할 GPU buffer 생성 & 새 buffer에 upload할 데이터를 담을 임시 container 준비
    ClothBufferSet rebuild_buffer_set = create_buffer_set(rebuild_element_counts, gl);
    BufferRebuildUploadData rebuild_upload_data = prepare_buffer_rebuild_upload_data(rebuild_element_counts, rebuild_garments.size());

    if (!rebuild_buffer_data(rebuild_garments, old_garments, old_buffer_set, rebuild_buffer_set, rebuild_upload_data, gl)) {
        delete_buffer_set(rebuild_buffer_set, gl);
        return;
    }

    // 준비한 data 값을 새 buffer에 upload
    upload_topology_data(rebuild_buffer_set, TopologyUploadOffsets{}, rebuild_upload_data.topology, gl);
    upload_stretch_constraints_data(rebuild_buffer_set, 0, rebuild_upload_data.stretch_constraints, gl);
    upload_bending_constraints_data(rebuild_buffer_set, 0, rebuild_upload_data.bending_constraints, gl);

    replace_with_rebuild_buffers(rebuild_buffer_set,
                                 make_buffer_rebuild_ranges(rebuild_garments),
                                 std::move(rebuild_upload_data.stretch_color_ranges),
                                 std::move(rebuild_upload_data.bending_color_ranges),
                                 rebuild_element_counts,
                                 gl);
}

void ClothGpuResources::replace_with_rebuild_buffers(ClothBufferSet rebuild_buffer_set,
                                                     std::vector<GarmentBufferRanges> rebuild_ranges,
                                                     std::vector<ConstraintRange> rebuild_stretch_color_ranges,
                                                     std::vector<ConstraintRange> rebuild_bending_color_ranges,
                                                     const ClothBufferElementCounts& rebuild_element_counts,
                                                     QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    buffers_ = rebuild_buffer_set;
    garments_ = std::move(rebuild_ranges);
    stretch_color_ranges_ = std::move(rebuild_stretch_color_ranges);
    bending_color_ranges_ = std::move(rebuild_bending_color_ranges);
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
    GarmentBufferRanges buffer_ranges = make_garment_buffer_ranges(
        garment,
        used_elements_,
        static_cast<std::uint32_t>(stretch_constraints.colorized_edges.size()),
        static_cast<std::uint32_t>(bending_constraints.colorized_edges.size())
    );
    const ClothBufferElementCounts next_used_elements = make_next_used_elements(used_elements_, buffer_ranges);

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

    // 3. garment GPU data 등록, buffer 사용량 update
    garments_.push_back(buffer_ranges);
    append_color_ranges(stretch_constraints.color_ranges, buffer_ranges.stretch_constraint_offset, stretch_color_ranges_);
    append_color_ranges(bending_constraints.color_ranges, buffer_ranges.bending_constraint_offset, bending_color_ranges_);
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
           required_elements.bending_constraint <= allocated_elements_.bending_constraint;
}
// GPU buffer 공간이 충분한지 확인 -> 부족하면 더 큰 buffer로 교체
void ClothGpuResources::ensure_capacity(const ClothBufferElementCounts& required_elements, QOpenGLFunctions_4_5_Core& gl)
{
    if (has_enough_capacity(required_elements)) {
        return;
    }

    const ClothBufferElementCounts next_allocated_elements = make_expanded_capacity(allocated_elements_, required_elements);
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
    if (buffers.vertex_normal != 0) {
        gl.glDeleteBuffers(1, &buffers.vertex_normal);
    }
    if (buffers.triangle_normal != 0) {
        gl.glDeleteBuffers(1, &buffers.triangle_normal);
    }
    if (buffers.adjacency_triangle != 0) {
        gl.glDeleteBuffers(1, &buffers.adjacency_triangle);
    }
    if (buffers.adjacency_offset != 0) {
        gl.glDeleteBuffers(1, &buffers.adjacency_offset);
    }
    if (buffers.stretch_rest_length != 0) {
        gl.glDeleteBuffers(1, &buffers.stretch_rest_length);
    }
    if (buffers.stretch_edge_index != 0) {
        gl.glDeleteBuffers(1, &buffers.stretch_edge_index);
    }
    if (buffers.bending_rest_length != 0) {
        gl.glDeleteBuffers(1, &buffers.bending_rest_length);
    }
    if (buffers.bending_edge_index != 0) {
        gl.glDeleteBuffers(1, &buffers.bending_edge_index);
    }
    if (buffers.index != 0) {
        gl.glDeleteBuffers(1, &buffers.index);
    }
    if (buffers.previous_position != 0) {
        gl.glDeleteBuffers(1, &buffers.previous_position);
    }
    if (buffers.current_position != 0) {
        gl.glDeleteBuffers(1, &buffers.current_position);
    }
    if (buffers.rest_position != 0) {
        gl.glDeleteBuffers(1, &buffers.rest_position);
    }
    if (buffers.vao != 0) {
        gl.glDeleteVertexArrays(1, &buffers.vao);
    }

    buffers = {};
}

void ClothGpuResources::delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(buffers_, gl);
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
                                 position_components  * static_cast<GLsizei>(sizeof(float)));
    gl.glEnableVertexArrayAttrib(buffers_.vao, position_attribute_location);
    gl.glVertexArrayAttribFormat(buffers_.vao, position_attribute_location, 3, GL_FLOAT, GL_FALSE, position_relative_offset);
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

void ClothGpuResources::draw_garment(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferRanges* buffer_ranges = find_garment_buffer_ranges(garment_id);
    if (!is_initialized() || buffer_ranges == nullptr || buffer_ranges->index_count == 0) {
        return;
    }

    const auto index_offset_bytes = static_cast<std::uintptr_t>(buffer_ranges->index_offset) * sizeof(std::uint32_t);
    gl.glBindVertexArray(buffers_.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(buffer_ranges->index_count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// state checks //
const GarmentBufferRanges* ClothGpuResources::find_garment_buffer_ranges(GarmentId garment_id) const
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(),
        [garment_id](const GarmentBufferRanges& buffer_ranges) {
            return buffer_ranges.id == garment_id;
        }
    );
    if (iter == garments_.end()) {
        return nullptr;
    }

    return &(*iter);
}

bool ClothGpuResources::has_gpu_objects() const
{
    return buffers_.vao != 0 &&
           buffers_.rest_position != 0 &&
           buffers_.current_position != 0 &&
           buffers_.previous_position != 0 &&
           buffers_.index != 0 &&
           buffers_.adjacency_offset != 0 &&
           buffers_.adjacency_triangle != 0 &&
           buffers_.stretch_edge_index != 0 &&
           buffers_.stretch_rest_length != 0 &&
           buffers_.bending_edge_index != 0 &&
           buffers_.bending_rest_length != 0 &&
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

ClothPositionBufferView ClothGpuResources::position_buffer_view() const
{
    ClothPositionBufferView view;
    view.rest_position_buffer = buffers_.rest_position;
    view.current_position_buffer = buffers_.current_position;
    view.previous_position_buffer = buffers_.previous_position;
    view.vertex_count = used_elements_.vertex;
    return view;
}

StretchConstraintBufferView ClothGpuResources::stretch_constraint_buffer_view() const
{
    StretchConstraintBufferView view;
    view.edge_index_buffer = buffers_.stretch_edge_index;
    view.rest_length_buffer = buffers_.stretch_rest_length;
    view.constraint_count = used_elements_.stretch_constraint;
    view.color_ranges = &stretch_color_ranges_;
    return view;
}

BendingConstraintBufferView ClothGpuResources::bending_constraint_buffer_view() const
{
    BendingConstraintBufferView view;
    view.edge_index_buffer = buffers_.bending_edge_index;
    view.rest_length_buffer = buffers_.bending_rest_length;
    view.constraint_count = used_elements_.bending_constraint;
    view.color_ranges = &bending_color_ranges_;
    return view;
}

MeshTopologyResources ClothGpuResources::mesh_topology_resources() const
{
    MeshTopologyResources topology;
    topology.position_buffer = buffers_.current_position;
    topology.index_buffer = buffers_.index;
    topology.adjacency_offset_buffer = buffers_.adjacency_offset;
    topology.adjacency_triangle_buffer = buffers_.adjacency_triangle;
    topology.position_component_offset = 0;
    topology.vertex_count = used_elements_.vertex;
    topology.triangle_count = used_elements_.triangle;
    return topology;
}

MeshNormalResources ClothGpuResources::mesh_normal_resources() const
{
    MeshNormalResources normals;
    normals.triangle_normal_buffer = buffers_.triangle_normal;
    normals.vertex_normal_buffer = buffers_.vertex_normal;
    return normals;
}
