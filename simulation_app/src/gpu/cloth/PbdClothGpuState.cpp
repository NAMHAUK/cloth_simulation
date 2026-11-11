#include "gpu/cloth/PbdClothGpuState.h"
#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SceneState.h"
#include "simulation/bvh/Bvh.h"
#include "utils/BufferUtils.h"
#include "utils/MeshGeometryUtils.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
void append_bvh_nodes(const Bvh& bvh,
                      std::uint32_t element_start_index,
                      std::vector<std::uint32_t>& level_offsets,
                      std::vector<BvhNode>& nodes)
{
    const auto root_node_index = static_cast<std::uint32_t>(nodes.size());
    for (std::uint32_t& level_offset : level_offsets) {
        level_offset += root_node_index;
    }
    for (BvhNode node : bvh.nodes) {
        if (node.is_leaf()) {
            node.first_element_index += element_start_index;
        } else {
            node.first_element_index += element_start_index;
            node.left_child_index += root_node_index;
            node.right_child_index += root_node_index;
        }
        nodes.push_back(node);
    }
}
}

// Dynamic state
void PbdClothGpuState::rebuild_dynamic_buffers(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl)
{
    release_dynamic_buffers(gl);

    // create new buffers
    const GLsizeiptr position_bytes = byte_size<glm::vec4>(cloth_state.element_counts().vertex);
    buffers_.previous_position = create_buffer(position_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    buffers_.collision_pushout = create_buffer(position_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
    buffers_.inward_motion_correction = create_buffer(position_bytes, nullptr, GL_DYNAMIC_DRAW, gl);
}

void PbdClothGpuState::reset_velocity(const ClothGpuState& cloth_state,
                                      std::uint32_t vertex_start_index,
                                      std::uint32_t vertex_count,
                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const GLsizeiptr offset = byte_size<glm::vec4>(vertex_start_index);
    const GLsizeiptr size = byte_size<glm::vec4>(vertex_count);

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.glCopyNamedBufferSubData(cloth_state.buffer_set().current_position,
                                buffers_.previous_position,
                                offset,
                                offset,
                                size);
    gl.glClearNamedBufferSubData(buffers_.collision_pushout, GL_RGBA32F, offset, size, GL_RGBA, GL_FLOAT, nullptr);
    gl.glClearNamedBufferSubData(buffers_.inward_motion_correction,
                                 GL_RGBA32F,
                                 offset,
                                 size,
                                 GL_RGBA,
                                 GL_FLOAT,
                                 nullptr);
}

// Attachment buffers
void PbdClothGpuState::rebuild_attachment_buffers(const std::vector<GarmentObject>& garments,
                                                  const std::vector<const GarmentObject*>& placement_garments,
                                                  QOpenGLFunctions_4_5_Core& gl)
{   
    // Calculate ranges
    std::array<AttachmentBufferState, garment_layer_count> attachment_states{};
    std::uint32_t attachment_count = 0;
    for (const GarmentObject& garment : garments) {
        AttachmentBufferState& state = attachment_states[garment.layer];
        state.start_index = attachment_count;
        state.count = garment.mesh.attachment_vertex_indices.size();
        attachment_count += state.count;
    }

    // Create buffers
    const std::uint32_t attachment_capacity = std::max(attachment_count, 1u);
    GLuint attachment_indices = 0;
    GLuint attachment_barycentric_offset = 0;
    attachment_indices = create_buffer(byte_size<glm::uvec2>(attachment_capacity), nullptr, GL_DYNAMIC_DRAW, gl);
    attachment_barycentric_offset = create_buffer(byte_size<glm::vec4>(attachment_capacity),
                                                  nullptr,
                                                  GL_DYNAMIC_DRAW,
                                                  gl);
    
    // Copy retained attachments
    for (const GarmentObject& garment : garments) {
        const AttachmentBufferState& source_state = attachment_states_[garment.layer];
        AttachmentBufferState& destination_state = attachment_states[garment.layer];
        if (source_state.active_count == 0u ||
            std::find(placement_garments.begin(), placement_garments.end(), &garment) != placement_garments.end()) {
            continue;
        }

        gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        gl.glCopyNamedBufferSubData(buffers_.attachment_indices,
                                    attachment_indices,
                                    byte_size<glm::uvec2>(source_state.start_index),
                                    byte_size<glm::uvec2>(destination_state.start_index),
                                    byte_size<glm::uvec2>(source_state.active_count));
        gl.glCopyNamedBufferSubData(buffers_.attachment_barycentric_offset,
                                    attachment_barycentric_offset,
                                    byte_size<glm::vec4>(source_state.start_index),
                                    byte_size<glm::vec4>(destination_state.start_index),
                                    byte_size<glm::vec4>(source_state.active_count));
        destination_state.active_count = source_state.active_count;
    }

    // Replace buffers
    release_attachment_buffers(gl);
    buffers_.attachment_indices = attachment_indices;
    buffers_.attachment_barycentric_offset = attachment_barycentric_offset;
    attachment_states_ = attachment_states;
}

void PbdClothGpuState::upload_attachment_indices(const GarmentObject& garment,
                                                 const ClothGpuState& cloth_state,
                                                 QOpenGLFunctions_4_5_Core& gl)
{
    AttachmentBufferState& attachment_state = attachment_states_[garment.layer];
    if (attachment_state.count == 0u) {
        attachment_state.active_count = 0u;
        return;
    }

    const GarmentBufferState& garment_state = cloth_state.garment_states()[garment.layer];
    std::vector<glm::uvec2> attachment_indices;
    attachment_indices.reserve(attachment_state.count);
    for (std::uint32_t local_vertex_index : garment.mesh.attachment_vertex_indices) {
        attachment_indices.push_back({garment_state.vertex_start_index + local_vertex_index, 0u});
    }

    gl.glNamedBufferSubData(buffers_.attachment_indices,
                            byte_size<glm::uvec2>(attachment_state.start_index),
                            byte_size<glm::uvec2>(attachment_state.count),
                            attachment_indices.data());
}

void PbdClothGpuState::activate_attachment_targets(GarmentLayer layer)
{
    AttachmentBufferState& attachment_state = attachment_states_[layer];
    attachment_state.active_count = attachment_state.count;
}

// Normal buffers
void PbdClothGpuState::rebuild_normal_buffers(const std::vector<TriangleVertexIndices>& triangle_vertex_indices,
                                              std::uint32_t vertex_count,
                                              QOpenGLFunctions_4_5_Core& gl)
{
    release_normal_buffers(gl);
    buffers_.triangle_normal = create_buffer(byte_size<glm::vec4>(triangle_vertex_indices.size()),
                                             nullptr,
                                             GL_DYNAMIC_DRAW,
                                             gl);

    const auto adjacency = build_vertex_triangle_adjacency(vertex_count, triangle_vertex_indices);

    buffers_.adjacent_triangle_offsets = create_buffer(byte_size<std::uint32_t>(adjacency.offsets.size()),
                                                       adjacency.offsets.data(),
                                                       GL_STATIC_DRAW,
                                                       gl);
    buffers_.adjacent_triangle_indices = create_buffer(byte_size<std::uint32_t>(adjacency.triangle_indices.size()),
                                                       adjacency.triangle_indices.data(),
                                                       GL_STATIC_DRAW,
                                                       gl);
}

// Collision buffers
void PbdClothGpuState::create_collision_buffers(const std::vector<GarmentObject>& garments,
                                                const ClothGpuState& cloth_state,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    release_collision_buffers(gl);
    create_bvh_buffers(garments, cloth_state, gl);
    create_exclusion_buffers(garments, cloth_state, gl);
}

void PbdClothGpuState::create_bvh_buffers(const std::vector<GarmentObject>& garments,
                                          const ClothGpuState& cloth_state,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    std::vector<BvhNode> triangle_nodes;
    std::vector<BvhNode> edge_nodes;
    std::vector<MeshEdge> edges;

    for (const GarmentObject& garment : garments) {
        auto& collision_state = collision_states_[garment.layer];
        const auto& garment_state = cloth_state.garment_states()[garment.layer];
        collision_state.edge_start_index = static_cast<std::uint32_t>(edges.size());
        collision_state.edge_count = static_cast<std::uint32_t>(garment.edge_bvh.edge_vertex_indices.size());
        for (const MeshEdge& edge : garment.edge_bvh.edge_vertex_indices) {
            edges.push_back(
                {garment_state.vertex_start_index + edge.vertex_a, garment_state.vertex_start_index + edge.vertex_b});
        }

        collision_state.triangle_bvh_level_offsets = garment.triangle_bvh.level_offsets;
        collision_state.edge_bvh_level_offsets = garment.edge_bvh.level_offsets;
        max_bvh_level_count_ = std::max({max_bvh_level_count_,
                                         collision_state.triangle_bvh_level_offsets.size() - 1u,
                                         collision_state.edge_bvh_level_offsets.size() - 1u});
        append_bvh_nodes(garment.triangle_bvh,
                         garment_state.triangle_start_index,
                         collision_state.triangle_bvh_level_offsets,
                         triangle_nodes);
        append_bvh_nodes(garment.edge_bvh,
                         collision_state.edge_start_index,
                         collision_state.edge_bvh_level_offsets,
                         edge_nodes);
    }

    buffers_.edge_vertex_indices = create_buffer(byte_size<MeshEdge>(edges.size()), edges.data(), GL_STATIC_DRAW, gl);
    buffers_.triangle_bvh_node = create_buffer(byte_size<BvhNode>(triangle_nodes.size()),
                                               triangle_nodes.data(),
                                               GL_DYNAMIC_DRAW,
                                               gl);
    buffers_.triangle_bounds = create_buffer(byte_size<Aabb>(cloth_state.element_counts().triangle),
                                             nullptr,
                                             GL_DYNAMIC_DRAW,
                                             gl);
    buffers_.edge_bvh_node = create_buffer(byte_size<BvhNode>(edge_nodes.size()),
                                           edge_nodes.data(),
                                           GL_DYNAMIC_DRAW,
                                           gl);
    buffers_.edge_bounds = create_buffer(byte_size<Aabb>(edges.size()), nullptr, GL_DYNAMIC_DRAW, gl);
}

void PbdClothGpuState::create_exclusion_buffers(const std::vector<GarmentObject>& garments,
                                                const ClothGpuState& cloth_state,
                                                QOpenGLFunctions_4_5_Core& gl)
{
    GLint64 max_block_size = 0;
    gl.glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_block_size);

    // Calculate exclusion offsets and sizes
    std::size_t vertex_face_word_count = 0;
    std::size_t edge_word_count = 0;
    for (const GarmentObject& garment : garments) {
        auto& state = collision_states_[garment.layer];
        const auto& garment_state = cloth_state.garment_states()[garment.layer];

        state.vertex_face_exclusion_offset = static_cast<std::uint32_t>(vertex_face_word_count);
        const std::size_t words_per_vertex = (static_cast<std::size_t>(garment_state.triangle_count) + 31u) / 32u;
        vertex_face_word_count += garment_state.vertex_count * words_per_vertex;

        if (vertex_face_word_count > std::numeric_limits<std::uint32_t>::max() ||
            byte_size<std::uint32_t>(vertex_face_word_count) > max_block_size) {
            throw std::runtime_error("Cloth vertex-face exclusion bitset exceeds the GPU SSBO size limit.");
        }

        state.edge_exclusion_offset = static_cast<std::uint32_t>(edge_word_count);
        const std::size_t words_per_edge = (static_cast<std::size_t>(state.edge_count) + 31u) / 32u;
        edge_word_count += state.edge_count * words_per_edge;

        if (edge_word_count > std::numeric_limits<std::uint32_t>::max() ||
            byte_size<std::uint32_t>(edge_word_count) > max_block_size) {
            throw std::runtime_error("Cloth edge exclusion bitset exceeds the GPU SSBO size limit.");
        }
    }

    // Create exclusion buffers
    {
        const GLsizeiptr buffer_size = byte_size<std::uint32_t>(vertex_face_word_count);
        GLuint& buffer = buffers_.vertex_face_exclusions;
        buffer = create_buffer(buffer_size, nullptr, GL_STATIC_DRAW, gl);

        GLint64 allocated_size = 0;
        gl.glGetNamedBufferParameteri64v(buffer, GL_BUFFER_SIZE, &allocated_size);
        if (allocated_size != buffer_size) {
            throw std::runtime_error("Failed to allocate cloth vertex-face exclusion bitset.");
        }
    }

    {
        const GLsizeiptr buffer_size = byte_size<std::uint32_t>(edge_word_count);
        GLuint& buffer = buffers_.edge_exclusions;
        buffer = create_buffer(buffer_size, nullptr, GL_STATIC_DRAW, gl);

        GLint64 allocated_size = 0;
        gl.glGetNamedBufferParameteri64v(buffer, GL_BUFFER_SIZE, &allocated_size);
        if (allocated_size != buffer_size) {
            throw std::runtime_error("Failed to allocate cloth edge exclusion bitset.");
        }
    }

    // Upload exclusions
    for (const GarmentObject& garment : garments) {
        const auto& state = collision_states_[garment.layer];
        const auto& garment_state = cloth_state.garment_states()[garment.layer];

        {
            const auto exclusions = build_vertex_neighborhood_masks(garment_state.vertex_count,
                                                                    garment.triangle_bvh.triangle_vertex_indices);

            gl.glNamedBufferSubData(buffers_.vertex_face_exclusions,
                                    byte_size<std::uint32_t>(state.vertex_face_exclusion_offset),
                                    byte_size<std::uint32_t>(exclusions.size()),
                                    exclusions.data());
        }

        {
            const auto exclusions = build_edge_exclusions(garment_state.vertex_count,
                                                          garment.edge_bvh.edge_vertex_indices);

            gl.glNamedBufferSubData(buffers_.edge_exclusions,
                                    byte_size<std::uint32_t>(state.edge_exclusion_offset),
                                    byte_size<std::uint32_t>(exclusions.size()),
                                    exclusions.data());
        }
    }
}

// Distance constraint buffers
void PbdClothGpuState::create_distance_constraint_buffers(const std::vector<GarmentObject>& garments,
                                                          const ClothGpuState& cloth_state,
                                                          QOpenGLFunctions_4_5_Core& gl)
{
    release_distance_constraint_buffers(gl);

    const auto create_buffers = [&](GarmentDistanceConstraints GarmentMesh::* constraints_member,
                                    std::uint32_t& constraint_count,
                                    std::vector<ConstraintColorState>& color_states,
                                    GLuint& vertex_indices_buffer,
                                    GLuint& rest_length_buffer) {
        std::vector<MeshEdge> edges;
        std::vector<float> rest_lengths;
        std::size_t color_count = 0;
        for (const GarmentObject& garment : garments) {
            const auto& constraints = garment.mesh.*constraints_member;
            constraint_count += static_cast<std::uint32_t>(constraints.colorized_edges.size());
            color_count = std::max(color_count, constraints.color_states.size());
        }
        edges.reserve(constraint_count);
        rest_lengths.reserve(constraint_count);
        color_states.reserve(color_count);

        for (std::size_t color_index = 0; color_index < color_count; ++color_index) {
            const auto color_start_index = static_cast<std::uint32_t>(edges.size());

            for (const GarmentObject& garment : garments) {
                const auto& constraints = garment.mesh.*constraints_member;
                if (color_index >= constraints.color_states.size()) {
                    continue;
                }

                const auto vertex_offset = cloth_state.garment_states()[garment.layer].vertex_start_index;
                const auto& color_state = constraints.color_states[color_index];
                for (std::uint32_t index = 0; index < color_state.count; ++index) {
                    const std::size_t constraint_index = std::size_t{color_state.start_index} + index;
                    const auto& edge = constraints.colorized_edges[constraint_index];

                    edges.push_back({vertex_offset + edge.vertex_a, vertex_offset + edge.vertex_b});
                    rest_lengths.push_back(constraints.rest_lengths[constraint_index]);
                }
            }

            const auto color_constraint_count = static_cast<std::uint32_t>(edges.size()) - color_start_index;
            color_states.push_back({color_start_index, color_constraint_count});
        }

        vertex_indices_buffer = create_buffer(byte_size<MeshEdge>(edges.size()), edges.data(), GL_STATIC_DRAW, gl);

        rest_length_buffer = create_buffer(byte_size<float>(rest_lengths.size()),
                                           rest_lengths.data(),
                                           GL_STATIC_DRAW,
                                           gl);
    };

    create_buffers(&GarmentMesh::stretch_constraints,
                   stretch_constraint_count_,
                   stretch_color_states_,
                   buffers_.stretch_edge_vertex_indices,
                   buffers_.stretch_rest_length);

    create_buffers(&GarmentMesh::bending_constraints,
                   bending_constraint_count_,
                   bending_color_states_,
                   buffers_.bending_edge_vertex_indices,
                   buffers_.bending_rest_length);
}

// State validation
bool PbdClothGpuState::is_initialized() const
{
    return stretch_constraint_count_ > 0 &&
           bending_constraint_count_ > 0 &&
           has_gpu_objects() &&
           !stretch_color_states_.empty() &&
           !bending_color_states_.empty();
}

bool PbdClothGpuState::has_gpu_objects() const
{
    return buffers_.adjacent_triangle_offsets != 0 &&
           buffers_.adjacent_triangle_indices != 0 &&
           buffers_.triangle_normal != 0 &&
           buffers_.triangle_bvh_node != 0 &&
           buffers_.triangle_bounds != 0 &&
           buffers_.vertex_face_exclusions != 0 &&
           buffers_.edge_vertex_indices != 0 &&
           buffers_.edge_bounds != 0 &&
           buffers_.edge_bvh_node != 0 &&
           buffers_.edge_exclusions != 0 &&
           buffers_.previous_position != 0 &&
           buffers_.collision_pushout != 0 &&
           buffers_.inward_motion_correction != 0 &&
           buffers_.stretch_edge_vertex_indices != 0 &&
           buffers_.stretch_rest_length != 0 &&
           buffers_.bending_edge_vertex_indices != 0 &&
           buffers_.bending_rest_length != 0 &&
           buffers_.attachment_indices != 0 &&
           buffers_.attachment_barycentric_offset != 0;
}

// Accessors
const PbdClothBufferSet& PbdClothGpuState::buffer_set() const
{
    return buffers_;
}

const std::array<AttachmentBufferState, garment_layer_count>& PbdClothGpuState::attachment_states() const
{
    return attachment_states_;
}

const std::array<GarmentCollisionBufferState, garment_layer_count>& PbdClothGpuState::collision_states() const
{
    return collision_states_;
}

std::uint32_t PbdClothGpuState::edge_count() const
{
    std::uint32_t count = 0;
    for (const auto& state : collision_states_) {
        count += state.edge_count;
    }
    return count;
}

std::size_t PbdClothGpuState::max_bvh_level_count() const
{
    return max_bvh_level_count_;
}

std::uint32_t PbdClothGpuState::stretch_constraint_count() const
{
    return stretch_constraint_count_;
}

std::uint32_t PbdClothGpuState::bending_constraint_count() const
{
    return bending_constraint_count_;
}

const std::vector<ConstraintColorState>& PbdClothGpuState::stretch_color_states() const
{
    return stretch_color_states_;
}

const std::vector<ConstraintColorState>& PbdClothGpuState::bending_color_states() const
{
    return bending_color_states_;
}

// Release
void PbdClothGpuState::release(QOpenGLFunctions_4_5_Core& gl)
{
    release_normal_buffers(gl);
    release_collision_buffers(gl);
    release_dynamic_buffers(gl);
    release_distance_constraint_buffers(gl);
    release_attachment_buffers(gl);
}

void PbdClothGpuState::release_dynamic_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers_.previous_position,
        buffers_.collision_pushout,
        buffers_.inward_motion_correction,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);
    buffers_.previous_position = 0;
    buffers_.collision_pushout = 0;
    buffers_.inward_motion_correction = 0;
}

void PbdClothGpuState::release_attachment_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &buffers_.attachment_indices);
    gl.glDeleteBuffers(1, &buffers_.attachment_barycentric_offset);
    buffers_.attachment_indices = 0;
    buffers_.attachment_barycentric_offset = 0;
    attachment_states_ = {};
}

void PbdClothGpuState::release_normal_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers_.adjacent_triangle_offsets,
        buffers_.adjacent_triangle_indices,
        buffers_.triangle_normal,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);
    buffers_.adjacent_triangle_offsets = 0;
    buffers_.adjacent_triangle_indices = 0;
    buffers_.triangle_normal = 0;
}

void PbdClothGpuState::release_collision_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers_.triangle_bvh_node,
        buffers_.triangle_bounds,
        buffers_.vertex_face_exclusions,
        buffers_.edge_vertex_indices,
        buffers_.edge_bounds,
        buffers_.edge_bvh_node,
        buffers_.edge_exclusions,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);

    buffers_.triangle_bvh_node = 0;
    buffers_.triangle_bounds = 0;
    buffers_.vertex_face_exclusions = 0;
    buffers_.edge_vertex_indices = 0;
    buffers_.edge_bounds = 0;
    buffers_.edge_bvh_node = 0;
    buffers_.edge_exclusions = 0;
    collision_states_ = {};
    max_bvh_level_count_ = 0;
}

void PbdClothGpuState::release_distance_constraint_buffers(QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint buffer_ids[] = {
        buffers_.stretch_edge_vertex_indices,
        buffers_.stretch_rest_length,
        buffers_.bending_edge_vertex_indices,
        buffers_.bending_rest_length,
    };
    gl.glDeleteBuffers(static_cast<GLsizei>(std::size(buffer_ids)), buffer_ids);

    buffers_.stretch_edge_vertex_indices = 0;
    buffers_.stretch_rest_length = 0;
    buffers_.bending_edge_vertex_indices = 0;
    buffers_.bending_rest_length = 0;
    stretch_constraint_count_ = 0;
    bending_constraint_count_ = 0;
    stretch_color_states_.clear();
    bending_color_states_.clear();
}
