#include "gpu/cloth/ClothGpuResources.h"

#include "scene/SceneState.h"
#include "utils/BufferUtils.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
static_assert(sizeof(MeshEdge) == sizeof(std::uint32_t) * 2u);

void clear_dynamic_state(const ClothBufferSet& buffers, ElementRange vertices, QOpenGLFunctions_4_5_Core& gl)
{
    const std::array<GLuint, 3> state_buffers{
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
    };
    const GLsizeiptr offset = byte_size<glm::vec4>(vertices.offset);
    const GLsizeiptr size = byte_size<glm::vec4>(vertices.count);

    for (GLuint buffer : state_buffers) {
        gl.glClearNamedBufferSubData(buffer, GL_RGBA32F, offset, size, GL_RGBA, GL_FLOAT, nullptr);
    }
}

ClothBufferSet create_dynamic_buffer_set(const ClothBufferElementCounts& counts,
                                         QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet buffers;
    const auto create_buffer = [&](GLuint& buffer, GLsizeiptr size) {
        gl.glCreateBuffers(1, &buffer);
        gl.glNamedBufferData(buffer, size, nullptr, GL_DYNAMIC_DRAW);
    };
    const std::uint32_t attachment_capacity = std::max(counts.attachment_constraint, 1u);

    gl.glCreateVertexArrays(1, &buffers.vao);
    create_buffer(buffers.current_position, byte_size<glm::vec4>(counts.vertex));
    create_buffer(buffers.previous_position, byte_size<glm::vec4>(counts.vertex));
    create_buffer(buffers.collision_pushout, byte_size<glm::vec4>(counts.vertex));
    create_buffer(buffers.cloth_cloth_pushout, byte_size<glm::vec4>(counts.vertex));
    create_buffer(buffers.contact_motion_delta, byte_size<glm::vec4>(counts.vertex));
    create_buffer(buffers.body_triangle_index, byte_size<std::uint32_t>(counts.vertex));
    create_buffer(buffers.attachment_indices, byte_size<glm::uvec2>(attachment_capacity));
    create_buffer(buffers.attachment_barycentric_offset, byte_size<glm::vec4>(attachment_capacity));
    create_buffer(buffers.triangle_normal, byte_size<glm::vec4>(counts.triangle));
    create_buffer(buffers.vertex_normal, byte_size<glm::vec4>(counts.vertex));

    return buffers;
}

void upload_vertex_positions(const ClothBufferSet& buffers,
                             const std::vector<float>& vertices,
                             ElementRange vertex_range,
                             QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(vertex_range.count);
    for (std::size_t index = 0; index < gpu_positions.size(); ++index) {
        const std::size_t offset = index * position_components;
        gpu_positions[index] = glm::vec4(vertices[offset], vertices[offset + 1], vertices[offset + 2], 0.0f);
    }

    gl.glNamedBufferSubData(buffers.current_position,
                            byte_size<glm::vec4>(vertex_range.offset),
                            byte_size<glm::vec4>(vertex_range.count),
                            gpu_positions.data());
    gl.glNamedBufferSubData(buffers.previous_position,
                            byte_size<glm::vec4>(vertex_range.offset),
                            byte_size<glm::vec4>(vertex_range.count),
                            gpu_positions.data());
    clear_dynamic_state(buffers, vertex_range, gl);
}

void upload_rest_lengths(const ClothBufferSet& buffers,
                         const GarmentObject& garment,
                         ElementRange stretch_constraint_range,
                         ElementRange bending_constraint_range,
                         QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(buffers.stretch_rest_length,
                            byte_size<float>(stretch_constraint_range.offset),
                            byte_size<float>(garment.mesh.stretch_constraints.rest_lengths.size()),
                            garment.mesh.stretch_constraints.rest_lengths.data());

    gl.glNamedBufferSubData(buffers.bending_rest_length,
                            byte_size<float>(bending_constraint_range.offset),
                            byte_size<float>(garment.mesh.bending_constraints.rest_lengths.size()),
                            garment.mesh.bending_constraints.rest_lengths.data());
}
}

// Buffer rebuild
void ClothGpuResources::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                        GarmentLayer changed_layer,
                                        QOpenGLFunctions_4_5_Core& gl)
{
    BufferState rebuild_state{};
    assign_buffer_ranges(garments, rebuild_state);

    create_dynamic_buffers(garments, changed_layer, rebuild_state, gl);
    create_topology_buffers(garments, rebuild_state, gl);
    create_distance_constraint_buffers(garments, rebuild_state, gl);

    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
    state_ = std::move(rebuild_state);
    configure_vao(gl);
}

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
        state.index_ranges[layer] =
            append_range(element_counts.triangle_vertex_index, mesh.triangle_vertex_indices.size());
        state.triangle_ranges[layer] = append_range(element_counts.triangle, mesh.adjacency.triangle_count);
        state.adjacent_triangle_index_ranges[layer] =
            append_range(element_counts.adjacent_triangle_index, mesh.adjacency.triangle_indices.size());
        state.stretch_constraint_ranges[layer] =
            append_range(element_counts.stretch_constraint, mesh.stretch_constraints.colorized_edges.size());
        state.bending_constraint_ranges[layer] =
            append_range(element_counts.bending_constraint, mesh.bending_constraints.colorized_edges.size());
        state.attachment_constraint_ranges[layer] =
            append_range(element_counts.attachment_constraint, mesh.attachment_vertex_indices.size());
    }
}

void ClothGpuResources::create_dynamic_buffers(const std::vector<GarmentObject>& garments,
                                               GarmentLayer changed_layer,
                                               BufferState& rebuild_state,
                                               QOpenGLFunctions_4_5_Core& gl) const
{
    rebuild_state.buffers = create_dynamic_buffer_set(rebuild_state.element_counts, gl);

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const ElementRange vertex_range = rebuild_state.vertex_ranges[layer];

        if (changed_layer == layer) {
            upload_vertex_positions(rebuild_state.buffers, garment.mesh.vertices, vertex_range, gl);
        } else {
            copy_dynamic_state_buffers(layer, rebuild_state, gl);
            copy_attachment_target_state(layer, rebuild_state, gl);
        }
    }
}

void ClothGpuResources::copy_dynamic_state_buffers(GarmentLayer layer,
                                                   const BufferState& rebuild_state,
                                                   QOpenGLFunctions_4_5_Core& gl) const
{
    const ElementRange source_range = state_.vertex_ranges[layer];
    const ElementRange destination_range = rebuild_state.vertex_ranges[layer];
    const std::array buffer_pairs{
        std::pair{state_.buffers.current_position, rebuild_state.buffers.current_position},
        std::pair{state_.buffers.previous_position, rebuild_state.buffers.previous_position},
        std::pair{state_.buffers.collision_pushout, rebuild_state.buffers.collision_pushout},
        std::pair{state_.buffers.cloth_cloth_pushout, rebuild_state.buffers.cloth_cloth_pushout},
        std::pair{state_.buffers.contact_motion_delta, rebuild_state.buffers.contact_motion_delta},
    };

    for (const auto& [source_buffer, destination_buffer] : buffer_pairs) {
        gl.glCopyNamedBufferSubData(source_buffer,
                                    destination_buffer,
                                    byte_size<glm::vec4>(source_range.offset),
                                    byte_size<glm::vec4>(destination_range.offset),
                                    byte_size<glm::vec4>(destination_range.count));
    }

    gl.glCopyNamedBufferSubData(state_.buffers.body_triangle_index,
                                rebuild_state.buffers.body_triangle_index,
                                byte_size<std::uint32_t>(source_range.offset),
                                byte_size<std::uint32_t>(destination_range.offset),
                                byte_size<std::uint32_t>(destination_range.count));
}

void ClothGpuResources::copy_attachment_target_state(GarmentLayer layer,
                                                     BufferState& rebuild_state,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    const ElementRange source_range = state_.attachment_constraint_ranges[layer];
    const ElementRange destination_range = rebuild_state.attachment_constraint_ranges[layer];
    const ElementRange attachment_range = state_.attachment_ranges[layer];
    if (attachment_range.count == 0u) {
        return;
    }

    gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.attachment_indices,
                                rebuild_state.buffers.attachment_indices,
                                byte_size<glm::uvec2>(source_range.offset),
                                byte_size<glm::uvec2>(destination_range.offset),
                                byte_size<glm::uvec2>(attachment_range.count));
    gl.glCopyNamedBufferSubData(state_.buffers.attachment_barycentric_offset,
                                rebuild_state.buffers.attachment_barycentric_offset,
                                byte_size<glm::vec4>(source_range.offset),
                                byte_size<glm::vec4>(destination_range.offset),
                                byte_size<glm::vec4>(attachment_range.count));

    rebuild_state.attachment_ranges[layer] = {destination_range.offset, attachment_range.count};
}

void ClothGpuResources::create_topology_buffers(const std::vector<GarmentObject>& garments,
                                                BufferState& rebuild_state,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet& buffers = rebuild_state.buffers;
    const ClothBufferElementCounts& counts = rebuild_state.element_counts;

    std::vector<std::uint32_t> triangle_vertex_indices(counts.triangle_vertex_index);
    std::vector<std::uint32_t> adjacent_triangle_offsets(static_cast<std::size_t>(counts.vertex) + 1u);
    std::vector<std::uint32_t> adjacent_triangle_indices(counts.adjacent_triangle_index);

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const GarmentMesh& mesh = garment.mesh;
        const VertexTriangleAdjacency& adjacency = mesh.adjacency;
        const std::uint32_t vertex_offset = rebuild_state.vertex_ranges[layer].offset;
        const std::uint32_t index_offset = rebuild_state.index_ranges[layer].offset;
        const std::uint32_t triangle_offset = rebuild_state.triangle_ranges[layer].offset;
        const std::uint32_t adjacency_offset = rebuild_state.adjacent_triangle_index_ranges[layer].offset;

        for (std::size_t index = 0; index < mesh.triangle_vertex_indices.size(); ++index) {
            triangle_vertex_indices[index_offset + index] =
                vertex_offset + mesh.triangle_vertex_indices[index];
        }
        for (std::size_t index = 0; index < adjacency.offsets.size(); ++index) {
            adjacent_triangle_offsets[vertex_offset + index] = adjacency_offset + adjacency.offsets[index];
        }
        for (std::size_t index = 0; index < adjacency.triangle_indices.size(); ++index) {
            adjacent_triangle_indices[adjacency_offset + index] =
                triangle_offset + adjacency.triangle_indices[index];
        }
    }

    gl.glCreateBuffers(1, &buffers.triangle_vertex_indices);
    gl.glNamedBufferData(buffers.triangle_vertex_indices,
                         byte_size<std::uint32_t>(triangle_vertex_indices.size()),
                         triangle_vertex_indices.data(),
                         GL_STATIC_DRAW);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_offsets);
    gl.glNamedBufferData(buffers.adjacent_triangle_offsets,
                         byte_size<std::uint32_t>(adjacent_triangle_offsets.size()),
                         adjacent_triangle_offsets.data(),
                         GL_STATIC_DRAW);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_indices);
    gl.glNamedBufferData(buffers.adjacent_triangle_indices,
                         byte_size<std::uint32_t>(adjacent_triangle_indices.size()),
                         adjacent_triangle_indices.data(),
                         GL_STATIC_DRAW);
}

void ClothGpuResources::create_distance_constraint_buffers(const std::vector<GarmentObject>& garments,
                                                           BufferState& rebuild_state,
                                                           QOpenGLFunctions_4_5_Core& gl)
{
    const auto create_buffers = [&](GarmentDistanceConstraints GarmentMesh::* constraints_member,
                                    std::uint32_t constraint_count,
                                    const std::array<ElementRange, 2>& constraint_ranges,
                                    std::vector<ElementRange>& color_ranges,
                                    GLuint& edge_index_buffer,
                                    GLuint& rest_length_buffer) {
        std::vector<MeshEdge> edges;
        std::vector<float> rest_lengths;
        edges.reserve(constraint_count);
        rest_lengths.reserve(constraint_count);

        for (const GarmentObject& garment : garments) {
            const GarmentDistanceConstraints& constraints = garment.mesh.*constraints_member;
            const std::uint32_t vertex_offset = rebuild_state.vertex_ranges[garment.layer].offset;
            const std::uint32_t constraint_offset = constraint_ranges[garment.layer].offset;

            for (const MeshEdge& edge : constraints.colorized_edges) {
                edges.push_back({vertex_offset + edge.vertex_a, vertex_offset + edge.vertex_b});
            }

            for (float rest_length : constraints.rest_lengths) {
                rest_lengths.push_back(rest_length);
            }

            for (const MeshElementRange& local_range : constraints.color_ranges) {
                color_ranges.push_back({constraint_offset + local_range.offset, local_range.count});
            }
        }

        gl.glCreateBuffers(1, &edge_index_buffer);
        gl.glNamedBufferData(edge_index_buffer,
                             byte_size<MeshEdge>(edges.size()),
                             edges.data(),
                             GL_STATIC_DRAW);
        gl.glCreateBuffers(1, &rest_length_buffer);
        gl.glNamedBufferData(rest_length_buffer,
                             byte_size<float>(rest_lengths.size()),
                             rest_lengths.data(),
                             GL_STATIC_DRAW);
    };

    create_buffers(&GarmentMesh::stretch_constraints,
                   rebuild_state.element_counts.stretch_constraint,
                   rebuild_state.stretch_constraint_ranges,
                   rebuild_state.stretch_color_ranges,
                   rebuild_state.buffers.stretch_edge_index,
                   rebuild_state.buffers.stretch_rest_length);

    create_buffers(&GarmentMesh::bending_constraints,
                   rebuild_state.element_counts.bending_constraint,
                   rebuild_state.bending_constraint_ranges,
                   rebuild_state.bending_color_ranges,
                   rebuild_state.buffers.bending_edge_index,
                   rebuild_state.buffers.bending_rest_length);
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
void ClothGpuResources::upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentLayer layer = garment.layer;
    upload_vertex_positions(state_.buffers, garment.mesh.vertices, state_.vertex_ranges[layer], gl);
    upload_rest_lengths(state_.buffers,
                        garment,
                        state_.stretch_constraint_ranges[layer],
                        state_.bending_constraint_ranges[layer],
                        gl);
}

ElementRange ClothGpuResources::upload_attachment_indices(const GarmentObject& garment,
                                                          QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentLayer layer = garment.layer;
    const ElementRange vertex_range = state_.vertex_ranges[layer];
    const ElementRange target_range = state_.attachment_constraint_ranges[layer];
    if (target_range.count == 0u) {
        state_.attachment_ranges[layer] = {};
        return target_range;
    }

    std::vector<glm::uvec2> attachment_indices;
    attachment_indices.reserve(target_range.count);
    for (std::uint32_t local_vertex_index : garment.mesh.attachment_vertex_indices) {
        attachment_indices.push_back({vertex_range.offset + local_vertex_index, 0u});
    }

    gl.glNamedBufferSubData(state_.buffers.attachment_indices,
                            byte_size<glm::uvec2>(target_range.offset),
                            byte_size<glm::uvec2>(target_range.count),
                            attachment_indices.data());
    return target_range;
}

void ClothGpuResources::activate_attachment_targets(GarmentLayer layer, const ElementRange& target_range)
{
    state_.attachment_ranges[layer] = target_range;
}

void ClothGpuResources::copy_current_positions_to_previous(QOpenGLFunctions_4_5_Core& gl) const
{
    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.current_position,
                                state_.buffers.previous_position,
                                0,
                                0,
                                position_bytes);
    clear_dynamic_state(state_.buffers, {0, state_.element_counts.vertex}, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Base Position Buffers
void ClothGpuResources::capture_base_positions(QOpenGLFunctions_4_5_Core& gl)
{
    clear_base_positions(gl);

    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);
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

    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);

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
           state_.element_counts.triangle_vertex_index > 0 &&
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
           state_.buffers.body_triangle_index != 0 &&
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

ClothBodyTriangleIndexBufferView ClothGpuResources::body_triangle_index_buffer_view() const
{
    ClothBodyTriangleIndexBufferView view;
    view.body_triangle_index_buffer = state_.buffers.body_triangle_index;
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
    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
    reset_resources();
}

void ClothGpuResources::delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers.current_position,
        buffers.previous_position,
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
        buffers.body_triangle_index,
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
