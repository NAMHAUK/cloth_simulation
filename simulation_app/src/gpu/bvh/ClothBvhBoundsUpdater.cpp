#include "gpu/bvh/ClothBvhBoundsUpdater.h"

#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr GLuint cloth_current_positions_binding = 0;
constexpr GLuint cloth_previous_positions_binding = 1;
constexpr GLuint cloth_triangle_indices_binding = 2;
constexpr GLuint cloth_bvh_nodes_binding = 3;
constexpr GLuint cloth_triangle_bounds_binding = 4;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;
}

void ClothBvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(
        shader_dir / "bvh" / "cloth_bounds_update.comp",
        "Cloth BVH bounds update",
        gl);
    level_first_node_index_location_ = gl.glGetUniformLocation(program_, "uLevelFirstNodeIndex");
    level_node_count_location_ = gl.glGetUniformLocation(program_, "uLevelNodeCount");
    bounds_margin_location_ = gl.glGetUniformLocation(program_, "uBoundsMargin");

    if (std::min({level_first_node_index_location_, level_node_count_location_, bounds_margin_location_}) <
        0) {
        throw std::runtime_error("Cloth BVH bounds update compute shader missing required uniforms.");
    }
}

bool ClothBvhBoundsUpdater::can_update(const SimulationGpuView& views, float bounds_margin) const
{
    const auto& motion_view = views.cloth_motion;
    const auto& topology = views.cloth_topology;
    const auto& bvh_view = views.cloth_bvh;
    return program_ != 0 &&
           is_valid_motion_view(motion_view) &&
           is_valid_cloth_mesh_topology_resource(topology) &&
           is_valid_bvh_buffer_view(bvh_view) &&
           topology.vertex_count == motion_view.vertex_count &&
           std::isfinite(bounds_margin) &&
           bounds_margin >= 0.0f;
}

void ClothBvhBoundsUpdater::update(const SimulationGpuView& views,
                                   float bounds_margin,
                                   QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_update(views, bounds_margin));

    const auto& motion_view = views.cloth_motion;
    const auto& bvh_view = views.cloth_bvh;

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_triangle_indices_binding,
                        views.cloth_topology.triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_bvh_nodes_binding, bvh_view.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_triangle_bounds_binding, bvh_view.bounds_buffer);
    gl.glProgramUniform1f(program_, bounds_margin_location_, bounds_margin);

    std::size_t level_count = 0;
    for (const GarmentBufferState& garment_state : views.garment_buffer_states) {
        if (!garment_state.bvh_level_offsets.empty()) {
            level_count = std::max(level_count, garment_state.bvh_level_offsets.size() - 1u);
        }
    }

    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        for (const GarmentBufferState& garment_state : views.garment_buffer_states) {
            if (level_index + 1u >= garment_state.bvh_level_offsets.size()) {
                continue;
            }

            const std::size_t offset_index = garment_state.bvh_level_offsets.size() - 1u - level_index;
            const std::uint32_t first_node_index = garment_state.bvh_level_offsets[offset_index - 1u];
            const std::uint32_t node_count = garment_state.bvh_level_offsets[offset_index] - first_node_index;

            gl.glProgramUniform1ui(program_, level_first_node_index_location_, first_node_index);
            gl.glProgramUniform1ui(program_, level_node_count_location_, node_count);
            gl.glDispatchCompute(compute_group_count(node_count, bvh_bounds_update_local_size), 1, 1);
        }

        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void ClothBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
}
