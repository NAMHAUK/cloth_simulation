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

// Buffer layout
GarmentBufferRanges make_garment_buffer_ranges(const GarmentObject& garment,
                                               const ClothBufferElementCounts& offsets,
                                               std::uint32_t stretch_constraint_count,
                                               std::uint32_t bending_constraint_count,
                                               std::uint32_t attachment_constraint_count)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    GarmentBufferRanges buffer_ranges;
    buffer_ranges.vertices.offset = offsets.vertex;
    buffer_ranges.vertices.count =
        static_cast<std::uint32_t>(garment.mesh.vertices.size() / vertex_position_components);
    buffer_ranges.indices.offset = offsets.index;
    buffer_ranges.indices.count = static_cast<std::uint32_t>(garment.mesh.triangle_vertex_indices.size());
    buffer_ranges.triangles.offset = offsets.triangle;
    buffer_ranges.triangles.count = adjacency.face_count;
    buffer_ranges.adjacency_entries.offset = offsets.adjacency_entry;
    buffer_ranges.adjacency_entries.count = static_cast<std::uint32_t>(adjacency.face_indices.size());
    buffer_ranges.stretch_constraints.offset = offsets.stretch_constraint;
    buffer_ranges.stretch_constraints.count = stretch_constraint_count;
    buffer_ranges.bending_constraints.offset = offsets.bending_constraint;
    buffer_ranges.bending_constraints.count = bending_constraint_count;
    buffer_ranges.attachment_constraints.offset = offsets.attachment_constraint;
    buffer_ranges.attachment_constraints.count = attachment_constraint_count;
    return buffer_ranges;
}

ClothBufferElementCounts make_next_used_elements(const ClothBufferElementCounts& used_elements,
                                                 const GarmentBufferRanges& appended_ranges)
{
    ClothBufferElementCounts next_used_elements = used_elements;
    next_used_elements.vertex += appended_ranges.vertices.count;
    next_used_elements.index += appended_ranges.indices.count;
    next_used_elements.triangle += appended_ranges.triangles.count;
    next_used_elements.adjacency_entry += appended_ranges.adjacency_entries.count;
    next_used_elements.stretch_constraint += appended_ranges.stretch_constraints.count;
    next_used_elements.bending_constraint += appended_ranges.bending_constraints.count;
    next_used_elements.attachment_constraint += appended_ranges.attachment_constraints.count;
    return next_used_elements;
}

// buffer에서 garment이 사용할 구간 계산
void assign_buffer_rebuild_ranges(const std::vector<GarmentObject>& garments,
                                  std::array<GarmentBufferRanges, 2>& rebuild_ranges,
                                  ClothBufferElementCounts& rebuild_element_counts)
{
    rebuild_ranges = {};
    rebuild_element_counts = {};

    // 각 garment에 대해 buffer에서 사용할 구간 계산 (누적 count를 통해 각 garment가 사용할 범위 저장)
    // rebuild_ranges에 layer별로 각 garment이 사용할 구간이 저장됨
    for (const GarmentObject& garment : garments) {
        const GarmentDistanceConstraints& stretch_constraints = garment.mesh.stretch_constraints;
        const GarmentDistanceConstraints& bending_constraints = garment.mesh.bending_constraints;
        const auto attachment_constraint_count =
            static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());

        GarmentBufferRanges& buffer_ranges = rebuild_ranges[garment.layer];
        buffer_ranges =
            make_garment_buffer_ranges(garment,
                                       rebuild_element_counts,
                                       static_cast<std::uint32_t>(stretch_constraints.colorized_edges.size()),
                                       static_cast<std::uint32_t>(bending_constraints.colorized_edges.size()),
                                       attachment_constraint_count);

        rebuild_element_counts = make_next_used_elements(rebuild_element_counts, buffer_ranges);
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
    gl.glNamedBufferData(buffers.index,
                         byte_size(allocated_elements.index, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_offsets,
                         byte_size(allocated_elements.vertex + 1u, sizeof(std::uint32_t)),
                         nullptr,
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(buffers.adjacent_triangle_indices,
                         byte_size(allocated_elements.adjacency_entry, sizeof(std::uint32_t)),
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
                                const GarmentBufferRanges& buffer_ranges,
                                TopologyUploadData& data)
{
    const VertexFaceAdjacency& adjacency = garment.mesh.adjacency;

    // vertex index: 각 garment triangle의 vertex index
    for (std::uint32_t local_index : garment.mesh.triangle_vertex_indices) {
        data.vertex_indices.push_back(buffer_ranges.vertices.offset + local_index);
    }

    // adjacent_triangle_offsets: 각 vertex의 adjacent triangle 배열에서 시작 위치
    for (std::uint32_t local_vertex = 0; local_vertex <= buffer_ranges.vertices.count; ++local_vertex) {
        data.adjacent_triangle_offsets[buffer_ranges.vertices.offset + local_vertex] =
            buffer_ranges.adjacency_entries.offset + adjacency.offsets[local_vertex];
    }

    // adjacent_triangle_indices: 각 vertex가 속한 triangle index 배열
    for (std::uint32_t local_face : adjacency.face_indices) {
        data.adjacent_triangle_indices.push_back(buffer_ranges.triangles.offset + local_face);
    }
}

// topology data를 GPU buffer에 upload
void upload_topology_data(const ClothBufferSet& buffers,
                          const TopologyUploadData& data,
                          QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(
        buffers.index,
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
                                           const GarmentBufferRanges& buffer_ranges,
                                           DistanceConstraintUploadData& data)
{
    for (const MeshEdge& edge : constraints.colorized_edges) {
        data.edge_indices.push_back(buffer_ranges.vertices.offset + edge.vertex_a);
        data.edge_indices.push_back(buffer_ranges.vertices.offset + edge.vertex_b);
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
                          const GarmentBufferRanges& buffer_ranges,
                          QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(buffer_ranges.vertices.count);
    for (std::uint32_t vertex_index = 0; vertex_index < buffer_ranges.vertices.count; ++vertex_index) {
        const std::size_t source_index = static_cast<std::size_t>(vertex_index) * vertex_position_components;
        gpu_positions[vertex_index] =
            glm::vec4(vertices[source_index], vertices[source_index + 1u], vertices[source_index + 2u], 0.0f);
    }

    const GLsizeiptr position_offset_bytes = byte_size(buffer_ranges.vertices.offset, sizeof(glm::vec4));
    const GLsizeiptr position_size_bytes = byte_size(buffer_ranges.vertices.count, sizeof(glm::vec4));

    gl.glNamedBufferSubData(buffers.current_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    gl.glNamedBufferSubData(buffers.previous_position,
                            position_offset_bytes,
                            position_size_bytes,
                            gpu_positions.data());
    clear_dynamic_state(buffers, buffer_ranges.vertices, gl);
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
        byte_size(buffer_ranges.stretch_constraints.offset, sizeof(float)),
        byte_size(static_cast<std::uint32_t>(stretch_constraints.rest_lengths.size()), sizeof(float)),
        stretch_constraints.rest_lengths.data());

    gl.glNamedBufferSubData(
        buffers.bending_rest_length,
        byte_size(buffer_ranges.bending_constraints.offset, sizeof(float)),
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

    build_topology_upload_data(garment, buffer_ranges, data.topology);
    build_distance_constraint_upload_data(stretch_constraints, buffer_ranges, data.stretch_constraints);
    build_distance_constraint_upload_data(bending_constraints, buffer_ranges, data.bending_constraints);

    append_color_ranges(stretch_constraints.color_ranges,
                        buffer_ranges.stretch_constraints.offset,
                        data.stretch_color_ranges);
    append_color_ranges(bending_constraints.color_ranges,
                        buffer_ranges.bending_constraints.offset,
                        data.bending_color_ranges);
}

// State preservation
void copy_dynamic_state_buffers(const GarmentBufferRanges& source_ranges,
                                const GarmentBufferRanges& destination_ranges,
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
    const GLsizeiptr source_offset_bytes = byte_size(source_ranges.vertices.offset, sizeof(glm::vec4));
    const GLsizeiptr destination_offset_bytes =
        byte_size(destination_ranges.vertices.offset, sizeof(glm::vec4));
    const GLsizeiptr state_size_bytes = byte_size(destination_ranges.vertices.count, sizeof(glm::vec4));
    for (std::size_t buffer_index = 0; buffer_index < source_state_buffers.size(); ++buffer_index) {
        gl.glCopyNamedBufferSubData(source_state_buffers[buffer_index],
                                    destination_state_buffers[buffer_index],
                                    source_offset_bytes,
                                    destination_offset_bytes,
                                    state_size_bytes);
    }

    const GLsizeiptr source_body_triangle_id_offset_bytes =
        byte_size(source_ranges.vertices.offset, sizeof(std::uint32_t));
    const GLsizeiptr destination_body_triangle_id_offset_bytes =
        byte_size(destination_ranges.vertices.offset, sizeof(std::uint32_t));
    const GLsizeiptr body_triangle_id_bytes =
        byte_size(destination_ranges.vertices.count, sizeof(std::uint32_t));
    gl.glCopyNamedBufferSubData(source_buffers.body_triangle_id,
                                destination_buffers.body_triangle_id,
                                source_body_triangle_id_offset_bytes,
                                destination_body_triangle_id_offset_bytes,
                                body_triangle_id_bytes);
}

std::uint32_t copy_attachment_target_buffers(const GarmentBufferRanges& old_data,
                                             const GarmentBufferRanges& next_data,
                                             const std::vector<ElementRange>& active_attachment_ranges,
                                             const ClothBufferSet& old_buffers,
                                             const ClothBufferSet& next_buffers,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    const auto range_iter = std::find_if(active_attachment_ranges.begin(),
                                         active_attachment_ranges.end(),
                                         [&old_data](const ElementRange& range) {
                                             return range.offset == old_data.attachment_constraints.offset;
                                         });
    if (range_iter == active_attachment_ranges.end()) {
        return 0u;
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    const GLsizeiptr old_index_offset_bytes =
        byte_size(old_data.attachment_constraints.offset, sizeof(glm::uvec2));
    const GLsizeiptr next_index_offset_bytes =
        byte_size(next_data.attachment_constraints.offset, sizeof(glm::uvec2));
    const GLsizeiptr index_size_bytes = byte_size(range_iter->count, sizeof(glm::uvec2));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_indices,
                                next_buffers.attachment_indices,
                                old_index_offset_bytes,
                                next_index_offset_bytes,
                                index_size_bytes);

    const GLsizeiptr old_barycentric_offset_bytes =
        byte_size(old_data.attachment_constraints.offset, sizeof(glm::vec4));
    const GLsizeiptr next_barycentric_offset_bytes =
        byte_size(next_data.attachment_constraints.offset, sizeof(glm::vec4));
    const GLsizeiptr barycentric_size_bytes = byte_size(range_iter->count, sizeof(glm::vec4));
    gl.glCopyNamedBufferSubData(old_buffers.attachment_barycentric_offset,
                                next_buffers.attachment_barycentric_offset,
                                old_barycentric_offset_bytes,
                                next_barycentric_offset_bytes,
                                barycentric_size_bytes);

    return range_iter->count;
}

void rebuild_buffer_data(const std::vector<GarmentObject>& garments,
                         const std::array<GarmentBufferRanges, 2>& rebuild_ranges,
                         const std::array<GarmentBufferRanges, 2>& old_garments,
                         const std::vector<ElementRange>& old_attachment_ranges,
                         const ClothBufferSet& old_buffer_set,
                         const ClothBufferSet& rebuild_buffer_set,
                         BufferRebuildUploadData& rebuild_upload_data,
                         std::optional<GarmentLayer> updated_layer,
                         QOpenGLFunctions_4_5_Core& gl)
{
    for (const GarmentObject& garment : garments) {
        const GarmentBufferRanges& buffer_ranges = rebuild_ranges[garment.layer];
        const GarmentLayer layer = garment.layer;
        const GarmentBufferRanges& old_data = old_garments[layer];
        const bool reset_garment = updated_layer == layer;

        if (reset_garment) {
            upload_position_data(rebuild_buffer_set, garment.mesh.vertices, buffer_ranges, gl);
        } else {
            copy_dynamic_state_buffers(old_data, buffer_ranges, old_buffer_set, rebuild_buffer_set, gl);
        }

        std::uint32_t copied_attachment_target_count = 0u;
        if (!reset_garment) {
            copied_attachment_target_count = copy_attachment_target_buffers(old_data,
                                                                            buffer_ranges,
                                                                            old_attachment_ranges,
                                                                            old_buffer_set,
                                                                            rebuild_buffer_set,
                                                                            gl);
        }
        if (copied_attachment_target_count > 0u) {
            rebuild_upload_data.attachment_ranges.push_back(
                {buffer_ranges.attachment_constraints.offset, copied_attachment_target_count});
        }

        build_buffer_rebuild_upload_data(garment, buffer_ranges, rebuild_upload_data);
    }
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
    base_positions_ = other.base_positions_;
    base_position_vertex_count_ = other.base_position_vertex_count_;

    other.reset_resources();
}

// Buffer management
void ClothGpuResources::update_garment_buffers(const std::vector<GarmentObject>& garments,
                                               std::optional<GarmentLayer> updated_layer,
                                               QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return;
    }

    if (updated_layer.has_value()) {
        rebuild_buffers(garments, updated_layer, gl);
        return;
    }

    // 1. GPU에만 있는 garment 발견 (scene에서 삭제된 것) -> buffer rebuild 필요
    bool needs_buffer_rebuild = false;
    for (std::size_t layer_index = 0; layer_index < garments_.size(); ++layer_index) {
        const GarmentBufferRanges& buffer_ranges = garments_[layer_index];
        if (!buffer_ranges.is_loaded()) {
            continue;
        }
        const auto layer = static_cast<GarmentLayer>(layer_index);
        const auto iter =
            std::find_if(garments.begin(), garments.end(), [layer](const GarmentObject& garment) {
                return garment.layer == layer;
            });

        if (iter == garments.end()) {
            needs_buffer_rebuild = true;
            break;
        }
    }
    if (needs_buffer_rebuild) {
        // 남길 garments 정보만 새로운 buffer에 재배치
        rebuild_buffers(garments, std::nullopt, gl);
    }
}

void ClothGpuResources::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                        std::optional<GarmentLayer> updated_layer,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    std::array<GarmentBufferRanges, 2> rebuild_ranges;
    ClothBufferElementCounts rebuild_element_counts;

    // 1. 새로운 GPU buffer에서 garment들이 사용할 구간 배정 (빈틈 없이 연속적으로)
    assign_buffer_rebuild_ranges(garments, rebuild_ranges, rebuild_element_counts);

    const ClothBufferSet old_buffer_set = buffers_;
    const std::array<GarmentBufferRanges, 2>& old_garments = garments_;
    const std::vector<ElementRange>& old_attachment_ranges = attachment_ranges_;

    // 2. 새롭게 할당할 GPU buffer 생성 & 새 buffer에 upload할 데이터를 담을 임시 container 준비
    ClothBufferSet rebuild_buffer_set = create_buffer_set(rebuild_element_counts, gl);
    BufferRebuildUploadData rebuild_upload_data =
        prepare_buffer_rebuild_upload_data(rebuild_element_counts, garments.size());

    rebuild_buffer_data(garments,
                        rebuild_ranges,
                        old_garments,
                        old_attachment_ranges,
                        old_buffer_set,
                        rebuild_buffer_set,
                        rebuild_upload_data,
                        updated_layer,
                        gl);

    // 준비한 data 값을 새 buffer에 upload
    upload_topology_data(rebuild_buffer_set, rebuild_upload_data.topology, gl);
    upload_distance_constraint_data(rebuild_buffer_set.stretch_edge_index,
                                    rebuild_buffer_set.stretch_rest_length,
                                    0,
                                    rebuild_upload_data.stretch_constraints,
                                    gl);
    upload_distance_constraint_data(rebuild_buffer_set.bending_edge_index,
                                    rebuild_buffer_set.bending_rest_length,
                                    0,
                                    rebuild_upload_data.bending_constraints,
                                    gl);

    replace_with_rebuild_buffers(rebuild_buffer_set,
                                 std::move(rebuild_ranges),
                                 std::move(rebuild_upload_data.stretch_color_ranges),
                                 std::move(rebuild_upload_data.bending_color_ranges),
                                 std::move(rebuild_upload_data.attachment_ranges),
                                 rebuild_element_counts,
                                 gl);
}

void ClothGpuResources::replace_with_rebuild_buffers(ClothBufferSet rebuild_buffer_set,
                                                     std::array<GarmentBufferRanges, 2> rebuild_ranges,
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

    configure_vao(gl);
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

void ClothGpuResources::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentBufferRanges& buffer_ranges = garments_[garment.layer];
    upload_position_data(buffers_, garment.mesh.vertices, buffer_ranges, gl);
    upload_rest_length_data(buffers_, garment, buffer_ranges, gl);
}

void ClothGpuResources::upload_garment_attachment_vertices(const GarmentObject& garment,
                                                           ElementRange& target_range,
                                                           QOpenGLFunctions_4_5_Core& gl)
{
    target_range = {};
    const GarmentBufferRanges& buffer_ranges = garments_[garment.layer];
    const auto attachment_constraint_count =
        static_cast<std::uint32_t>(garment.mesh.attachment_vertex_indices.size());
    target_range = {buffer_ranges.attachment_constraints.offset, attachment_constraint_count};
    if (attachment_constraint_count == 0u) {
        set_attachment_range(buffer_ranges.attachment_constraints.offset, 0u, attachment_ranges_);
        return;
    }

    std::vector<glm::uvec2> attachment_indices;
    attachment_indices.reserve(attachment_constraint_count);
    for (std::uint32_t local_vertex_index : garment.mesh.attachment_vertex_indices) {
        attachment_indices.push_back({buffer_ranges.vertices.offset + local_vertex_index, 0u});
    }

    gl.glNamedBufferSubData(buffers_.attachment_indices,
                            byte_size(buffer_ranges.attachment_constraints.offset, sizeof(glm::uvec2)),
                            byte_size(attachment_constraint_count, sizeof(glm::uvec2)),
                            attachment_indices.data());
}

void ClothGpuResources::activate_attachment_targets(const ElementRange& target_range)
{
    set_attachment_range(target_range.offset, target_range.count, attachment_ranges_);
}

void ClothGpuResources::capture_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    clear_base_positions(gl);

    const GLsizeiptr position_bytes = byte_size(used_elements_.vertex, sizeof(glm::vec4));
    gl.glCreateBuffers(1, &base_positions_);
    gl.glNamedBufferData(base_positions_, position_bytes, nullptr, GL_DYNAMIC_COPY);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(buffers_.current_position, base_positions_, 0, 0, position_bytes);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    base_position_vertex_count_ = used_elements_.vertex;
}

bool ClothGpuResources::restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const
{
    if (base_positions_ == 0 || base_position_vertex_count_ != used_elements_.vertex) {
        return false;
    }

    const GLsizeiptr position_bytes = byte_size(used_elements_.vertex, sizeof(glm::vec4));

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(base_positions_, buffers_.current_position, 0, 0, position_bytes);
    gl.glCopyNamedBufferSubData(base_positions_, buffers_.previous_position, 0, 0, position_bytes);
    clear_dynamic_state(buffers_, {0, used_elements_.vertex}, gl);
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
    const GLsizeiptr position_bytes = byte_size(used_elements_.vertex, sizeof(glm::vec4));

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(buffers_.current_position, buffers_.previous_position, 0, 0, position_bytes);
    clear_dynamic_state(buffers_, {0, used_elements_.vertex}, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Rendering
void ClothGpuResources::bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_index, buffers_.vertex_normal);
}

void ClothGpuResources::draw_garment(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferRanges& buffer_ranges = garments_[layer];
    const auto index_offset_bytes =
        static_cast<std::uintptr_t>(buffer_ranges.indices.offset) * sizeof(std::uint32_t);
    gl.glBindVertexArray(buffers_.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(buffer_ranges.indices.count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// State
bool ClothGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           std::any_of(garments_.begin(),
                       garments_.end(),
                       [](const GarmentBufferRanges& ranges) { return ranges.is_loaded(); }) &&
           used_elements_.vertex > 0 &&
           used_elements_.index > 0 &&
           used_elements_.triangle > 0 &&
           used_elements_.stretch_constraint > 0 &&
           used_elements_.bending_constraint > 0 &&
           !stretch_color_ranges_.empty() &&
           !bending_color_ranges_.empty();
}

bool ClothGpuResources::has_gpu_objects() const
{
    return buffers_.vao != 0 &&
           buffers_.current_position != 0 &&
           buffers_.previous_position != 0 &&
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

// Accessors

std::array<ElementRange, 2> ClothGpuResources::garment_vertex_ranges() const
{
    return {garments_[GarmentLayer::Lower].vertices, garments_[GarmentLayer::Upper].vertices};
}

ClothMotionBufferView ClothGpuResources::motion_buffer_view() const
{
    ClothMotionBufferView view;
    view.current_position_buffer = buffers_.current_position;
    view.previous_position_buffer = buffers_.previous_position;
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

// Release
void ClothGpuResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_gpu_objects(gl);
    reset_resources();
}

void ClothGpuResources::delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffer_set(buffers_, gl);
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
        buffers.index,
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
    buffers_ = {};
    garments_ = {};
    stretch_color_ranges_.clear();
    bending_color_ranges_.clear();
    attachment_ranges_.clear();
    used_elements_ = {};

    base_positions_ = 0;
    base_position_vertex_count_ = 0;
}
