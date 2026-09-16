#include "gpu/scene/SimulationBufferBindings.h"

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {
namespace binding {

// Cloth: 0 ~ 18
namespace cloth {
constexpr GLuint current_positions = 0;
constexpr GLuint previous_positions = 1;
constexpr GLuint collision_pushouts = 2;
constexpr GLuint cloth_pushouts = 3;
constexpr GLuint contact_motion_deltas = 4;
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

// Character: 19 ~ 35
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

// Collision: 36 ~ 50
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
}

template <std::size_t Size>
void bind_buffers(GLuint first,
                  const std::array<GLuint, Size>& buffers,
                  const char* owner,
                  QOpenGLFunctions_4_5_Core& gl)
{
    if (std::any_of(buffers.begin(), buffers.end(), [](GLuint buffer) { return buffer == 0; })) {
        throw std::runtime_error(std::string{"Cannot bind "} + owner + " buffer object is missing.");
    }

    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, first, buffers.size(), buffers.data());
}

void bind_dummy_buffers(GLuint first, GLsizei count, GLuint dummy_buffer, QOpenGLFunctions_4_5_Core& gl)
{
    std::array<GLuint, binding::count> buffers;
    buffers.fill(dummy_buffer);
    gl.glBindBuffersBase(GL_SHADER_STORAGE_BUFFER, first, count, buffers.data());
}

}

void SimulationBufferBindings::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    GLint max_bindings = 0;
    gl.glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &max_bindings);

    if (max_bindings < binding::count) {
        throw std::runtime_error("Insufficient SSBO bindings: required=" +
                                 std::to_string(binding::count) +
                                 ", reported=" +
                                 std::to_string(max_bindings) +
                                 '.');
    }

    constexpr std::array<std::uint32_t, 4> dummy_data{};
    gl.glCreateBuffers(1, &dummy_buffer_);
    gl.glNamedBufferData(dummy_buffer_, sizeof(dummy_data), dummy_data.data(), GL_STATIC_DRAW);
    bind_dummy_buffers(0, binding::count, dummy_buffer_, gl);
}

// binding

void SimulationBufferBindings::bind_character(const CharacterBufferSet& buffers,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
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
    bind_buffers(binding::character::start, binding_buffers, "character", gl);
}

void SimulationBufferBindings::bind_cloth(const ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl) const
{
    const std::array<GLuint, binding::cloth::count> binding_buffers{
        buffers.current_position,
        buffers.previous_position,
        buffers.collision_pushout,
        buffers.cloth_cloth_pushout,
        buffers.contact_motion_delta,
        dummy_buffer_, // Reserved binding 5.
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
    bind_buffers(binding::cloth::start, binding_buffers, "cloth", gl);
}

void SimulationBufferBindings::bind_collision(const CollisionBuffers& buffers,
                                              bool cloth_cloth_active,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
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
        cloth_cloth_active ? buffers.cloth_cloth_vertex_face.candidate_buffer : dummy_buffer_,
        cloth_cloth_active ? buffers.cloth_cloth_vertex_face.count_buffer : dummy_buffer_,
        cloth_cloth_active ? buffers.cloth_cloth_vertex_face.dispatch_size_buffer : dummy_buffer_,
        buffers.normal_correction_sum_buffer,
        buffers.friction_correction_sum_buffer,
        buffers.contact_motion_delta_sum_buffer,
    };
    bind_buffers(binding::collision::start, binding_buffers, "collision", gl);
}

// Binding reset

void SimulationBufferBindings::reset_character_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers(binding::character::start, binding::character::count, dummy_buffer_, gl);
}

void SimulationBufferBindings::reset_cloth_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers(binding::cloth::start, binding::cloth::count, dummy_buffer_, gl);
}

void SimulationBufferBindings::reset_collision_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers(binding::collision::start, binding::collision::count, dummy_buffer_, gl);
}

// release

void SimulationBufferBindings::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &dummy_buffer_);
    dummy_buffer_ = 0;
}
