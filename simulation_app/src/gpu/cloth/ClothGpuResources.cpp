#include "gpu/cloth/ClothGpuResources.h"

#include "asset/MeshGeometryUtils.h"
#include "scene/SceneState.h"
#include "utils/BufferUtils.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
static_assert(sizeof(MeshEdge) == sizeof(std::uint32_t) * 2u);
static_assert(sizeof(ConstraintColorState) == sizeof(std::uint32_t) * 2u);

void clear_dynamic_state(const ClothBufferSet& buffers,
                         std::uint32_t vertex_start_index,
                         std::uint32_t vertex_count,
                         QOpenGLFunctions_4_5_Core& gl)
{
    const std::array<GLuint, 3> state_buffers{
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
    };
    const GLsizeiptr offset = byte_size<glm::vec4>(vertex_start_index);
    const GLsizeiptr size = byte_size<glm::vec4>(vertex_count);

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
                             const GarmentBufferState& garment_state,
                             QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<glm::vec4> gpu_positions(garment_state.vertex_count);
    for (std::size_t index = 0; index < gpu_positions.size(); ++index) {
        const std::size_t offset = index * position_components;
        gpu_positions[index] = glm::vec4(vertices[offset], vertices[offset + 1], vertices[offset + 2], 0.0f);
    }

    gl.glNamedBufferSubData(buffers.current_position,
                            byte_size<glm::vec4>(garment_state.vertex_start_index),
                            byte_size<glm::vec4>(garment_state.vertex_count),
                            gpu_positions.data());
    gl.glNamedBufferSubData(buffers.previous_position,
                            byte_size<glm::vec4>(garment_state.vertex_start_index),
                            byte_size<glm::vec4>(garment_state.vertex_count),
                            gpu_positions.data());
    clear_dynamic_state(buffers, garment_state.vertex_start_index, garment_state.vertex_count, gl);
}

void upload_rest_lengths(const ClothBufferSet& buffers,
                         const GarmentObject& garment,
                         const GarmentBufferState& garment_state,
                         QOpenGLFunctions_4_5_Core& gl)
{
    gl.glNamedBufferSubData(buffers.stretch_rest_length,
                            byte_size<float>(garment_state.stretch_constraint_start_index),
                            byte_size<float>(garment.mesh.stretch_constraints.rest_lengths.size()),
                            garment.mesh.stretch_constraints.rest_lengths.data());

    gl.glNamedBufferSubData(buffers.bending_rest_length,
                            byte_size<float>(garment_state.bending_constraint_start_index),
                            byte_size<float>(garment.mesh.bending_constraints.rest_lengths.size()),
                            garment.mesh.bending_constraints.rest_lengths.data());
}

void append_bvh_nodes(const Bvh& bvh, GarmentBufferState& garment_state, std::vector<BvhNode>& nodes)
{
    const auto root_node_index = static_cast<std::uint32_t>(nodes.size());
    for (std::uint32_t& level_offset : garment_state.bvh_level_offsets) {
        level_offset += root_node_index;
    }
    for (BvhNode node : bvh.nodes) {
        if (node.is_leaf()) {
            node.first_element_index += garment_state.triangle_start_index;
        } else {
            node.left_child_index += root_node_index;
            node.right_child_index += root_node_index;
        }
        nodes.push_back(node);
    }
}
}

// Buffer rebuild
ClothBufferElementCounts ClothGpuResources::rebuild_buffers(const std::vector<GarmentObject>& garments,
                                                            GarmentLayer changed_layer,
                                                            QOpenGLFunctions_4_5_Core& gl)
{
    BufferState rebuild_state{};
    assign_garment_buffer_states(garments, rebuild_state);

    create_dynamic_buffers(garments, changed_layer, rebuild_state, gl);
    create_topology_buffers(garments, rebuild_state, gl);
    create_bvh_buffers(garments, rebuild_state, gl);
    create_distance_constraint_buffers(garments, rebuild_state, gl);

    delete_buffer_set(state_.buffers, gl);
    clear_base_positions(gl);
    state_ = std::move(rebuild_state);
    configure_vao(gl);
    return state_.element_counts;
}

void ClothGpuResources::assign_garment_buffer_states(const std::vector<GarmentObject>& garments,
                                                     BufferState& state)
{
    const auto append_elements = [](std::uint32_t& total_count, std::uint32_t count) {
        const std::uint32_t start_index = total_count;
        total_count += count;
        return start_index;
    };

    for (const GarmentObject& garment : garments) {
        const GarmentLayer layer = garment.layer;
        const auto& mesh = garment.mesh;
        const auto& triangle_vertex_indices = garment.triangle_bvh.indices;
        auto& element_counts = state.element_counts;
        GarmentBufferState& garment_state = state.garments[layer];

        garment_state.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / position_components);
        garment_state.vertex_start_index = append_elements(element_counts.vertex, garment_state.vertex_count);
        garment_state.triangle_count = static_cast<std::uint32_t>(triangle_vertex_indices.size() / 3u);
        garment_state.triangle_start_index =
            append_elements(element_counts.triangle, garment_state.triangle_count);
        garment_state.stretch_constraint_count =
            static_cast<std::uint32_t>(mesh.stretch_constraints.colorized_edges.size());
        garment_state.stretch_constraint_start_index =
            append_elements(element_counts.stretch_constraint, garment_state.stretch_constraint_count);
        garment_state.bending_constraint_count =
            static_cast<std::uint32_t>(mesh.bending_constraints.colorized_edges.size());
        garment_state.bending_constraint_start_index =
            append_elements(element_counts.bending_constraint, garment_state.bending_constraint_count);
        garment_state.attachment_constraint_count =
            static_cast<std::uint32_t>(mesh.attachment_vertex_indices.size());
        garment_state.attachment_constraint_start_index =
            append_elements(element_counts.attachment_constraint, garment_state.attachment_constraint_count);
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
        const GarmentBufferState& garment_state = rebuild_state.garments[layer];

        if (changed_layer == layer) {
            upload_vertex_positions(rebuild_state.buffers, garment.mesh.vertices, garment_state, gl);
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
    const GarmentBufferState& source_state = state_.garments[layer];
    const GarmentBufferState& destination_state = rebuild_state.garments[layer];
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
                                    byte_size<glm::vec4>(source_state.vertex_start_index),
                                    byte_size<glm::vec4>(destination_state.vertex_start_index),
                                    byte_size<glm::vec4>(destination_state.vertex_count));
    }

    gl.glCopyNamedBufferSubData(state_.buffers.body_triangle_index,
                                rebuild_state.buffers.body_triangle_index,
                                byte_size<std::uint32_t>(source_state.vertex_start_index),
                                byte_size<std::uint32_t>(destination_state.vertex_start_index),
                                byte_size<std::uint32_t>(destination_state.vertex_count));
}

void ClothGpuResources::copy_attachment_target_state(GarmentLayer layer,
                                                     BufferState& rebuild_state,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& source_state = state_.garments[layer];
    GarmentBufferState& destination_state = rebuild_state.garments[layer];
    if (source_state.active_attachment_constraint_count == 0u) {
        return;
    }

    gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(state_.buffers.attachment_indices,
                                rebuild_state.buffers.attachment_indices,
                                byte_size<glm::uvec2>(source_state.attachment_constraint_start_index),
                                byte_size<glm::uvec2>(destination_state.attachment_constraint_start_index),
                                byte_size<glm::uvec2>(source_state.active_attachment_constraint_count));
    gl.glCopyNamedBufferSubData(state_.buffers.attachment_barycentric_offset,
                                rebuild_state.buffers.attachment_barycentric_offset,
                                byte_size<glm::vec4>(source_state.attachment_constraint_start_index),
                                byte_size<glm::vec4>(destination_state.attachment_constraint_start_index),
                                byte_size<glm::vec4>(source_state.active_attachment_constraint_count));

    destination_state.active_attachment_constraint_count = source_state.active_attachment_constraint_count;
}

void ClothGpuResources::create_topology_buffers(const std::vector<GarmentObject>& garments,
                                                BufferState& rebuild_state,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    ClothBufferSet& buffers = rebuild_state.buffers;
    const ClothBufferElementCounts& counts = rebuild_state.element_counts;

    std::vector<std::uint32_t> triangle_vertex_indices;
    triangle_vertex_indices.reserve(static_cast<std::size_t>(counts.triangle) * 3u);

    for (const GarmentObject& garment : garments) {
        const std::uint32_t vertex_start_index = rebuild_state.garments[garment.layer].vertex_start_index;
        for (std::uint32_t vertex_index : garment.triangle_bvh.indices) {
            triangle_vertex_indices.push_back(vertex_start_index + vertex_index);
        }
    }

    VertexTriangleAdjacency adjacency;
    if (!build_vertex_triangle_adjacency(counts.vertex, triangle_vertex_indices, adjacency)) {
        delete_buffer_set(rebuild_state.buffers, gl);
        throw std::runtime_error("Failed to build cloth topology buffers.");
    }

    gl.glCreateBuffers(1, &buffers.triangle_vertex_indices);
    gl.glNamedBufferData(buffers.triangle_vertex_indices,
                         byte_size<std::uint32_t>(triangle_vertex_indices.size()),
                         triangle_vertex_indices.data(),
                         GL_STATIC_DRAW);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_offsets);
    gl.glNamedBufferData(buffers.adjacent_triangle_offsets,
                         byte_size<std::uint32_t>(adjacency.offsets.size()),
                         adjacency.offsets.data(),
                         GL_STATIC_DRAW);
    gl.glCreateBuffers(1, &buffers.adjacent_triangle_indices);
    gl.glNamedBufferData(buffers.adjacent_triangle_indices,
                         byte_size<std::uint32_t>(adjacency.triangle_indices.size()),
                         adjacency.triangle_indices.data(),
                         GL_STATIC_DRAW);
}

void ClothGpuResources::create_bvh_buffers(const std::vector<GarmentObject>& garments,
                                           BufferState& rebuild_state,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<BvhNode> nodes;

    for (const GarmentObject& garment : garments) {
        const Bvh& bvh = garment.triangle_bvh;
        GarmentBufferState& garment_state = rebuild_state.garments[garment.layer];
        garment_state.bvh_level_offsets = bvh.level_offsets;

        append_bvh_nodes(bvh, garment_state, nodes);
    }
    gl.glCreateBuffers(1, &rebuild_state.buffers.bvh_node);
    gl.glNamedBufferData(rebuild_state.buffers.bvh_node,
                         byte_size<BvhNode>(nodes.size()),
                         nodes.data(),
                         GL_DYNAMIC_DRAW);
    gl.glCreateBuffers(1, &rebuild_state.buffers.triangle_bounds);
    gl.glNamedBufferData(rebuild_state.buffers.triangle_bounds,
                         byte_size<Aabb>(rebuild_state.element_counts.triangle),
                         nullptr,
                         GL_DYNAMIC_DRAW);
}

void ClothGpuResources::create_distance_constraint_buffers(const std::vector<GarmentObject>& garments,
                                                           BufferState& rebuild_state,
                                                           QOpenGLFunctions_4_5_Core& gl)
{
    const auto create_buffers = [&](GarmentDistanceConstraints GarmentMesh::* constraints_member,
                                    std::uint32_t constraint_count,
                                    std::uint32_t GarmentBufferState::* constraint_start_index_member,
                                    std::vector<ConstraintColorState>& color_states,
                                    GLuint& edge_index_buffer,
                                    GLuint& rest_length_buffer) {
        std::vector<MeshEdge> edges;
        std::vector<float> rest_lengths;
        edges.reserve(constraint_count);
        rest_lengths.reserve(constraint_count);

        for (const GarmentObject& garment : garments) {
            const GarmentDistanceConstraints& constraints = garment.mesh.*constraints_member;
            const GarmentBufferState& garment_state = rebuild_state.garments[garment.layer];
            const std::uint32_t vertex_start_index = garment_state.vertex_start_index;
            const std::uint32_t constraint_start_index = garment_state.*constraint_start_index_member;

            for (const MeshEdge& edge : constraints.colorized_edges) {
                edges.push_back({vertex_start_index + edge.vertex_a, vertex_start_index + edge.vertex_b});
            }

            for (float rest_length : constraints.rest_lengths) {
                rest_lengths.push_back(rest_length);
            }

            for (const ConstraintColorState& local_color_state : constraints.color_states) {
                color_states.push_back(
                    {constraint_start_index + local_color_state.start_index, local_color_state.count});
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
                   &GarmentBufferState::stretch_constraint_start_index,
                   rebuild_state.stretch_color_states,
                   rebuild_state.buffers.stretch_edge_index,
                   rebuild_state.buffers.stretch_rest_length);

    create_buffers(&GarmentMesh::bending_constraints,
                   rebuild_state.element_counts.bending_constraint,
                   &GarmentBufferState::bending_constraint_start_index,
                   rebuild_state.bending_color_states,
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
    const GarmentBufferState& garment_state = state_.garments[layer];
    upload_vertex_positions(state_.buffers, garment.mesh.vertices, garment_state, gl);
    upload_rest_lengths(state_.buffers, garment, garment_state, gl);
}

void ClothGpuResources::upload_attachment_indices(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl)
{
    const GarmentLayer layer = garment.layer;
    GarmentBufferState& garment_state = state_.garments[layer];
    if (garment_state.attachment_constraint_count == 0u) {
        garment_state.active_attachment_constraint_count = 0u;
        return;
    }

    std::vector<glm::uvec2> attachment_indices;
    attachment_indices.reserve(garment_state.attachment_constraint_count);
    for (std::uint32_t local_vertex_index : garment.mesh.attachment_vertex_indices) {
        attachment_indices.push_back({garment_state.vertex_start_index + local_vertex_index, 0u});
    }

    gl.glNamedBufferSubData(state_.buffers.attachment_indices,
                            byte_size<glm::uvec2>(garment_state.attachment_constraint_start_index),
                            byte_size<glm::uvec2>(garment_state.attachment_constraint_count),
                            attachment_indices.data());
}

void ClothGpuResources::activate_attachment_targets(GarmentLayer layer)
{
    GarmentBufferState& garment_state = state_.garments[layer];
    garment_state.active_attachment_constraint_count = garment_state.attachment_constraint_count;
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
    clear_dynamic_state(state_.buffers, 0, state_.element_counts.vertex, gl);
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

void ClothGpuResources::restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const
{
    if (base_positions_ == 0 || base_position_vertex_count_ != state_.element_counts.vertex) {
        throw std::runtime_error("Failed to restore garment base positions.");
    }

    const GLsizeiptr position_bytes = byte_size<glm::vec4>(state_.element_counts.vertex);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(base_positions_, state_.buffers.current_position, 0, 0, position_bytes);
    gl.glCopyNamedBufferSubData(base_positions_, state_.buffers.previous_position, 0, 0, position_bytes);
    clear_dynamic_state(state_.buffers, 0, state_.element_counts.vertex, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
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
    const GarmentBufferState& garment_state = state_.garments[layer];
    const auto index_offset_bytes =
        static_cast<std::uintptr_t>(garment_state.triangle_start_index) * 3u * sizeof(std::uint32_t);
    gl.glBindVertexArray(state_.buffers.vao);
    gl.glDrawElements(GL_TRIANGLES,
                      static_cast<GLsizei>(garment_state.triangle_count * 3u),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(index_offset_bytes));
}

// State
bool ClothGpuResources::is_initialized() const
{
    return has_gpu_objects() &&
           std::any_of(
               state_.garments.begin(),
               state_.garments.end(),
               [](const GarmentBufferState& garment_state) { return garment_state.vertex_count != 0u; }) &&
           state_.element_counts.vertex > 0 &&
           state_.element_counts.triangle > 0 &&
           state_.element_counts.stretch_constraint > 0 &&
           state_.element_counts.bending_constraint > 0 &&
           !state_.stretch_color_states.empty() &&
           !state_.bending_color_states.empty();
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
           state_.buffers.vertex_normal != 0 &&
           state_.buffers.bvh_node != 0 &&
           state_.buffers.triangle_bounds != 0;
}

// Accessors

const std::array<GarmentBufferState, 2>& ClothGpuResources::garment_buffer_states() const
{
    return state_.garments;
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
    view.color_states = &state_.stretch_color_states;
    return view;
}

DistanceConstraintBufferView ClothGpuResources::bending_constraint_buffer_view() const
{
    DistanceConstraintBufferView view;
    view.edge_index_buffer = state_.buffers.bending_edge_index;
    view.rest_length_buffer = state_.buffers.bending_rest_length;
    view.constraint_count = state_.element_counts.bending_constraint;
    view.color_states = &state_.bending_color_states;
    return view;
}

AttachmentConstraintBufferView ClothGpuResources::attachment_constraint_buffer_view() const
{
    AttachmentConstraintBufferView view;
    view.attachment_index_buffer = state_.buffers.attachment_indices;
    view.barycentric_offset_buffer = state_.buffers.attachment_barycentric_offset;
    view.constraint_count = state_.element_counts.attachment_constraint;
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

BvhBufferView ClothGpuResources::cloth_bvh_buffer_view() const
{
    return {state_.buffers.bvh_node, state_.buffers.triangle_bounds};
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
        buffers.bvh_node,
        buffers.triangle_bounds,
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
