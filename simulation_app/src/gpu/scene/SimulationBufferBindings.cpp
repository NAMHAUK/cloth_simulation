#include "gpu/scene/SimulationBufferBindings.h"

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace binding {

// Cloth: 0..18
namespace cloth {
constexpr GLuint current_positions = 0;
constexpr GLuint previous_positions = 1;
constexpr GLuint collision_pushouts = 2;
constexpr GLuint cloth_pushouts = 3;
constexpr GLuint contact_motion_deltas = 4;
constexpr GLuint body_triangle_indices = 5;
constexpr GLuint triangle_vertex_indices = 6;
constexpr GLuint adjacent_triangle_offsets = 7;
constexpr GLuint adjacent_triangle_indices = 8;
constexpr GLuint stretch_edge_indices = 9;
constexpr GLuint stretch_rest_lengths = 10;
constexpr GLuint bending_edge_indices = 11;
constexpr GLuint bending_rest_lengths = 12;
constexpr GLuint attachment_indices = 13;
constexpr GLuint attachment_barycentric_offsets = 14;
constexpr GLuint triangle_normals = 15;
constexpr GLuint vertex_normals = 16;
constexpr GLuint bvh_nodes = 17;
constexpr GLuint triangle_bounds = 18;
constexpr GLuint start = current_positions;
constexpr GLuint end = triangle_bounds + 1;
constexpr std::size_t count = end - start;
}

// Character: 19..35
namespace character {
constexpr GLuint all_frame_positions = 19;
constexpr GLuint previous_positions = 20;
constexpr GLuint current_positions = 21;
constexpr GLuint triangle_indices = 22;
constexpr GLuint body_triangle_bvh_nodes = 23;
constexpr GLuint body_triangle_bounds = 24;
constexpr GLuint body_vertex_bvh_nodes = 25;
constexpr GLuint body_bvh_vertex_indices = 26;
constexpr GLuint body_vertex_bounds = 27;
constexpr GLuint body_edge_bvh_nodes = 28;
constexpr GLuint body_edge_indices = 29;
constexpr GLuint body_edge_bounds = 30;
constexpr GLuint adjacent_triangle_offsets = 31;
constexpr GLuint adjacent_triangle_indices = 32;
constexpr GLuint body_triangle_positions = 33;
constexpr GLuint body_triangle_normals = 34;
constexpr GLuint vertex_normals = 35;
constexpr GLuint start = all_frame_positions;
constexpr GLuint end = vertex_normals + 1;
constexpr std::size_t count = end - start;
}

// Collision: 36..50
namespace collision {
constexpr GLuint vertex_body_face_candidates = 36;
constexpr GLuint vertex_body_face_count = 37;
constexpr GLuint vertex_body_face_dispatch = 38;
constexpr GLuint edge_body_edge_candidates = 39;
constexpr GLuint edge_body_edge_count = 40;
constexpr GLuint edge_body_edge_dispatch = 41;
constexpr GLuint face_body_vertex_candidates = 42;
constexpr GLuint face_body_vertex_count = 43;
constexpr GLuint face_body_vertex_dispatch = 44;
constexpr GLuint cloth_vertex_face_candidates = 45;
constexpr GLuint cloth_vertex_face_count = 46;
constexpr GLuint cloth_vertex_face_dispatch = 47;
constexpr GLuint normal_correction_sums = 48;
constexpr GLuint friction_correction_sums = 49;
constexpr GLuint contact_motion_delta_sums = 50;
constexpr GLuint start = vertex_body_face_candidates;
constexpr GLuint end = contact_motion_delta_sums + 1;
constexpr std::size_t count = end - start;
}

constexpr std::size_t count = collision::end;

static_assert(cloth::end == character::start);
static_assert(character::end == collision::start);
static_assert(collision::end == count);
}

constexpr GLint required_compute_blocks = 14;
constexpr GLint required_vertex_blocks = 3;

template <std::size_t Size>
void require_complete_range(const std::array<GLuint, Size>& buffers, const char* owner)
{
    if (std::any_of(buffers.begin(), buffers.end(), [](GLuint buffer) { return buffer == 0; })) {
        throw std::runtime_error(
            std::string{"Cannot bind "} + owner + " SSBO range: buffer object is missing.");
    }
}

void require_initialized(GLuint fallback_buffer)
{
    if (fallback_buffer == 0) {
        throw std::runtime_error("Simulation SSBO bindings are not initialized.");
    }
}

void bind_fallback_range(GLuint first, GLsizei count, GLuint fallback_buffer, QOpenGLFunctions_4_5_Core& gl)
{
    if (fallback_buffer == 0) {
        return;
    }

    std::array<GLuint, binding::count> buffers;
    buffers.fill(fallback_buffer);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, first, count, buffers.data());
}

bool has_complete_candidate_buffers(const CollisionCandidateBuffers& buffers)
{
    return buffers.candidate_buffer != 0 && buffers.count_buffer != 0 && buffers.dispatch_size_buffer != 0;
}

bool has_any_candidate_buffer(const CollisionCandidateBuffers& buffers)
{
    return buffers.candidate_buffer != 0 || buffers.count_buffer != 0 || buffers.dispatch_size_buffer != 0;
}
}

// Lifecycle

void SimulationBufferBindings::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    GLint max_bindings = 0;
    GLint max_compute_blocks = 0;
    GLint max_vertex_blocks = 0;
    gl.glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &max_bindings);
    gl.glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &max_compute_blocks);
    gl.glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &max_vertex_blocks);

    if (max_bindings < binding::count ||
        max_compute_blocks < required_compute_blocks ||
        max_vertex_blocks < required_vertex_blocks) {
        throw std::runtime_error("Insufficient SSBO limits: bindings required=" +
                                 std::to_string(binding::count) +
                                 ", reported=" +
                                 std::to_string(max_bindings) +
                                 "; compute blocks required=" +
                                 std::to_string(required_compute_blocks) +
                                 ", reported=" +
                                 std::to_string(max_compute_blocks) +
                                 "; vertex blocks required=" +
                                 std::to_string(required_vertex_blocks) +
                                 ", reported=" +
                                 std::to_string(max_vertex_blocks) +
                                 '.');
    }

    std::cout << "SSBO limits: bindings=" << max_bindings << ", compute blocks=" << max_compute_blocks
              << ", vertex blocks=" << max_vertex_blocks << std::endl;

    constexpr std::array<std::uint32_t, 4> fallback_data{};
    gl.glCreateBuffers(1, &fallback_buffer_);
    gl.glNamedBufferData(fallback_buffer_, sizeof(fallback_data), fallback_data.data(), GL_STATIC_DRAW);
    bind_fallback_range(0, binding::count, fallback_buffer_, gl);
}

void SimulationBufferBindings::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &fallback_buffer_);
    fallback_buffer_ = 0;
}

// Owner ranges

void SimulationBufferBindings::bind_character(const CharacterBufferSet& buffers,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    require_initialized(fallback_buffer_);
    const std::array<GLuint, binding::character::count> binding_buffers{
        buffers.all_frame_positions,
        buffers.previous_position,
        buffers.current_position,
        buffers.triangle_index,
        buffers.body_triangle_bvh_node,
        buffers.body_triangle_bounds,
        buffers.body_vertex_bvh_node,
        buffers.body_vertex_bvh_vertex_index,
        buffers.body_vertex_bounds,
        buffers.body_edge_bvh_node,
        buffers.body_edge_index,
        buffers.body_edge_bounds,
        buffers.adjacent_triangle_offsets,
        buffers.adjacent_triangle_indices,
        buffers.triangle_position,
        buffers.triangle_normal,
        buffers.vertex_normal,
    };
    require_complete_range(binding_buffers, "character");
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER,
                         binding::character::start,
                         binding_buffers.size(),
                         binding_buffers.data());
}

void SimulationBufferBindings::bind_cloth(const ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl) const
{
    require_initialized(fallback_buffer_);
    const std::array<GLuint, binding::cloth::count> binding_buffers{
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
    require_complete_range(binding_buffers, "cloth");
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER,
                         binding::cloth::start,
                         binding_buffers.size(),
                         binding_buffers.data());
}

void SimulationBufferBindings::bind_collision(const CollisionBuffers& buffers,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    require_initialized(fallback_buffer_);
    const CollisionCandidateBuffers& cloth_cloth = buffers.cloth_cloth_vertex_face;
    if (has_any_candidate_buffer(cloth_cloth) && !has_complete_candidate_buffers(cloth_cloth)) {
        throw std::runtime_error(
            "Cannot bind collision SSBO range: optional cloth-cloth buffers are incomplete.");
    }

    const GLuint cloth_cloth_candidate =
        cloth_cloth.candidate_buffer != 0 ? cloth_cloth.candidate_buffer : fallback_buffer_;
    const GLuint cloth_cloth_count =
        cloth_cloth.count_buffer != 0 ? cloth_cloth.count_buffer : fallback_buffer_;
    const GLuint cloth_cloth_dispatch =
        cloth_cloth.dispatch_size_buffer != 0 ? cloth_cloth.dispatch_size_buffer : fallback_buffer_;
    const std::array<GLuint, binding::collision::count> binding_buffers{
        buffers.cloth_vertex_body_face.candidate_buffer,
        buffers.cloth_vertex_body_face.count_buffer,
        buffers.cloth_vertex_body_face.dispatch_size_buffer,
        buffers.cloth_edge_body_edge.candidate_buffer,
        buffers.cloth_edge_body_edge.count_buffer,
        buffers.cloth_edge_body_edge.dispatch_size_buffer,
        buffers.cloth_face_body_vertex.candidate_buffer,
        buffers.cloth_face_body_vertex.count_buffer,
        buffers.cloth_face_body_vertex.dispatch_size_buffer,
        cloth_cloth_candidate,
        cloth_cloth_count,
        cloth_cloth_dispatch,
        buffers.normal_correction_sum_buffer,
        buffers.friction_correction_sum_buffer,
        buffers.contact_motion_delta_sum_buffer,
    };
    require_complete_range(binding_buffers, "collision");
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER,
                         binding::collision::start,
                         binding_buffers.size(),
                         binding_buffers.data());
}

// Fallback restoration

void SimulationBufferBindings::restore_character(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_fallback_range(binding::character::start, binding::character::count, fallback_buffer_, gl);
}

void SimulationBufferBindings::restore_cloth(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_fallback_range(binding::cloth::start, binding::cloth::count, fallback_buffer_, gl);
}

void SimulationBufferBindings::restore_collision(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_fallback_range(binding::collision::start, binding::collision::count, fallback_buffer_, gl);
}
