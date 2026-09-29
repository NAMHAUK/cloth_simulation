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

// Cloth: 0 ~ 20
namespace cloth {
constexpr GLuint current_positions = 0;
constexpr GLuint previous_positions = 1;
constexpr GLuint collision_pushouts = 2;
constexpr GLuint inward_motion_corrections = 3;
constexpr GLuint edge_exclusions = 4;
constexpr GLuint triangle_vertex_indices = 5;
constexpr GLuint adjacent_triangle_offsets = 6;
constexpr GLuint adjacent_triangle_indices = 7;
constexpr GLuint stretch_edge_indices = 8;
constexpr GLuint stretch_rest_lengths = 9;
constexpr GLuint bending_edge_indices = 10;
constexpr GLuint bending_rest_lengths = 11;
constexpr GLuint attachment_indices = 12;
constexpr GLuint attachment_barycentric_offsets = 13;
constexpr GLuint triangle_normals = 14;
constexpr GLuint vertex_normals = 15;
constexpr GLuint bvh_nodes = 16;
constexpr GLuint triangle_bounds = 17;
constexpr GLuint start = current_positions;
constexpr GLuint edge_indices = 18;
constexpr GLuint edge_bounds = 19;
constexpr GLuint edge_bvh_nodes = 20;
constexpr GLuint end = edge_bvh_nodes + 1;
constexpr std::size_t count = end - start;
}

// Character: 21 ~ 34
namespace character {
constexpr GLuint all_frame_positions = 21;
constexpr GLuint previous_positions = 22;
constexpr GLuint current_positions = 23;
constexpr GLuint triangle_indices = 24;
constexpr GLuint body_triangle_bvh_nodes = 25;
constexpr GLuint body_triangle_bounds = 26;
constexpr GLuint body_edge_bvh_nodes = 27;
constexpr GLuint body_edge_indices = 28;
constexpr GLuint body_edge_bounds = 29;
constexpr GLuint adjacent_triangle_offsets = 30;
constexpr GLuint adjacent_triangle_indices = 31;
constexpr GLuint body_triangle_positions = 32;
constexpr GLuint body_triangle_normals = 33;
constexpr GLuint vertex_normals = 34;
constexpr GLuint start = all_frame_positions;
constexpr GLuint end = vertex_normals + 1;
constexpr std::size_t count = end - start;
}

// Collision: 35 ~ 49
namespace collision {
constexpr GLuint vertex_body_face_candidates = 35;
constexpr GLuint vertex_body_face_count = 36;
constexpr GLuint vertex_body_face_dispatch = 37;
constexpr GLuint edge_body_edge_candidates = 38;
constexpr GLuint edge_body_edge_count = 39;
constexpr GLuint edge_body_edge_dispatch = 40;
constexpr GLuint cloth_vertex_face_candidates = 41;
constexpr GLuint cloth_vertex_face_count = 42;
constexpr GLuint cloth_vertex_face_dispatch = 43;
constexpr GLuint normal_correction_sums = 44;
constexpr GLuint friction_correction_sums = 45;
constexpr GLuint inward_motion_correction_sums = 46;
constexpr GLuint start = vertex_body_face_candidates;
constexpr GLuint cloth_edge_edge_candidates = 47;
constexpr GLuint cloth_edge_edge_count = 48;
constexpr GLuint cloth_edge_edge_dispatch = 49;
constexpr GLuint end = cloth_edge_edge_dispatch + 1;
constexpr std::size_t count = end - start;
}

constexpr GLuint vertex_face_exclusions = 57;
constexpr std::size_t count = vertex_face_exclusions + 1;

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
        buffers.inward_motion_correction,
        buffers.edge_exclusions,
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
        buffers.triangle_bvh_node,
        buffers.triangle_bounds,
        buffers.edge_index,
        buffers.edge_bounds,
        buffers.edge_bvh_node,
    };
    bind_buffers(binding::cloth::start, binding_buffers, "cloth", gl);
    bind_buffers(binding::vertex_face_exclusions, std::array{buffers.vertex_face_exclusions}, "cloth", gl);
}

void SimulationBufferBindings::bind_collision(const CollisionBuffers& buffers,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    const std::array<GLuint, binding::collision::count> binding_buffers{
        buffers.cloth_vertex_body_face.candidate_buffer,
        buffers.cloth_vertex_body_face.count_buffer,
        buffers.cloth_vertex_body_face.dispatch_size_buffer,
        buffers.cloth_edge_body_edge.candidate_buffer,
        buffers.cloth_edge_body_edge.count_buffer,
        buffers.cloth_edge_body_edge.dispatch_size_buffer,
        buffers.cloth_cloth_vertex_face.candidate_buffer,
        buffers.cloth_cloth_vertex_face.count_buffer,
        buffers.cloth_cloth_vertex_face.dispatch_size_buffer,
        buffers.normal_correction_sum_buffer,
        buffers.friction_correction_sum_buffer,
        buffers.inward_motion_correction_sum_buffer,
        buffers.cloth_cloth_edge_edge.candidate_buffer,
        buffers.cloth_cloth_edge_edge.count_buffer,
        buffers.cloth_cloth_edge_edge.dispatch_size_buffer,
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
    bind_dummy_buffers(binding::vertex_face_exclusions, 1, dummy_buffer_, gl);
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
