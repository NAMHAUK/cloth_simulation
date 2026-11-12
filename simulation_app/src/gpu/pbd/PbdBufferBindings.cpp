#include "gpu/pbd/PbdBufferBindings.h"
#include "gpu/common/CharacterGpuState.h"
#include "gpu/common/ClothGpuState.h"
#include "gpu/pbd/PbdGpuState.h"
#include "utils/BufferUtils.h"

#include <algorithm>
#include <array>

namespace {
namespace binding {

// Cloth: 3 ~ 23
namespace cloth {
constexpr GLuint start = common_buffer_binding::character_vertex_normals + 1;
constexpr std::size_t count = 21;
}

// Character: 24 ~ 35
namespace character {
constexpr GLuint start = cloth::start + cloth::count;
constexpr std::size_t count = 12;
}

// Collision: 36 ~ 50
namespace collision {
constexpr GLuint start = character::start + character::count;
constexpr std::size_t count = 15;
}

constexpr std::size_t count = collision::start + collision::count;
}

}

void PbdBufferBindings::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    require_ssbo_bindings(binding::count, gl);
    dummy_buffer_ = create_dummy_buffer(gl);
    bind_dummy_buffers<binding::count>(0, dummy_buffer_, gl);
}

// binding

void PbdBufferBindings::bind_character(const CharacterRenderBuffers& buffers,
                                       const PbdCharacterBufferSet& pbd_buffers,
                                       QOpenGLFunctions_4_5_Core& gl) const
{
    const std::array<GLuint, binding::character::count> binding_buffers{
        pbd_buffers.animation_positions,
        pbd_buffers.previous_position,
        buffers.triangle_vertex_indices,
        pbd_buffers.body_triangle_bvh_node,
        pbd_buffers.body_triangle_bounds,
        pbd_buffers.body_edge_bvh_node,
        pbd_buffers.body_edge_vertex_indices,
        pbd_buffers.body_edge_bounds,
        pbd_buffers.adjacent_triangle_offsets,
        pbd_buffers.adjacent_triangle_indices,
        pbd_buffers.triangle_position,
        pbd_buffers.triangle_normal,
    };
    bind_buffers(common_buffer_binding::character_current_positions,
                 std::array{buffers.current_position, buffers.vertex_normal},
                 gl);
    bind_buffers(binding::character::start, binding_buffers, gl);
}

void PbdBufferBindings::bind_cloth(const ClothRenderBuffers& buffers,
                                   const PbdClothBufferSet& pbd_buffers,
                                   QOpenGLFunctions_4_5_Core& gl) const
{
    std::array<GLuint, binding::cloth::count> binding_buffers{
        buffers.current_position,
        pbd_buffers.previous_position,
        pbd_buffers.collision_pushout,
        pbd_buffers.inward_motion_correction,
        pbd_buffers.edge_exclusions,
        buffers.triangle_vertex_indices,
        pbd_buffers.adjacent_triangle_offsets,
        pbd_buffers.adjacent_triangle_indices,
        pbd_buffers.stretch_edge_vertex_indices,
        pbd_buffers.stretch_rest_length,
        pbd_buffers.bending_edge_vertex_indices,
        pbd_buffers.bending_rest_length,
        pbd_buffers.attachment_indices,
        pbd_buffers.attachment_barycentric_offset,
        pbd_buffers.triangle_normal,
        pbd_buffers.triangle_bvh_node,
        pbd_buffers.triangle_bounds,
        pbd_buffers.edge_vertex_indices,
        pbd_buffers.edge_bounds,
        pbd_buffers.edge_bvh_node,
        pbd_buffers.vertex_face_exclusions,
    };
    std::replace(binding_buffers.begin(), binding_buffers.end(), GLuint{0}, dummy_buffer_);
    bind_buffers(binding::cloth::start, binding_buffers, gl);
    bind_buffers(common_buffer_binding::cloth_vertex_normals,
                 std::array{buffers.vertex_normal != 0 ? buffers.vertex_normal : dummy_buffer_},
                 gl);
}

void PbdBufferBindings::bind_collision(const CollisionBuffers& buffers, QOpenGLFunctions_4_5_Core& gl) const
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
    bind_buffers(binding::collision::start, binding_buffers, gl);
}

// Binding reset

void PbdBufferBindings::reset_character_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers<2>(common_buffer_binding::character_current_positions, dummy_buffer_, gl);
    bind_dummy_buffers<binding::character::count>(binding::character::start, dummy_buffer_, gl);
}

void PbdBufferBindings::reset_cloth_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers<binding::cloth::count>(binding::cloth::start, dummy_buffer_, gl);
    bind_dummy_buffers<1>(common_buffer_binding::cloth_vertex_normals, dummy_buffer_, gl);
}

void PbdBufferBindings::reset_collision_bindings(QOpenGLFunctions_4_5_Core& gl) const
{
    bind_dummy_buffers<binding::collision::count>(binding::collision::start, dummy_buffer_, gl);
}

void PbdBufferBindings::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &dummy_buffer_);
    dummy_buffer_ = 0;
}
