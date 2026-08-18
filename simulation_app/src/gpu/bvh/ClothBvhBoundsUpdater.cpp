#include "gpu/bvh/ClothBvhBoundsUpdater.h"

#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <array>
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

bool has_valid_bvh_levels(const GarmentBvhState& bvh_state)
{
    if (bvh_state.levels.empty()) {
        return false;
    }

    std::uint32_t expected_node_end_index = bvh_state.first_node_index + bvh_state.node_count;
    for (const BvhLevelState& level_state : bvh_state.levels) {
        if (level_state.node_count == 0 ||
            level_state.first_node_index < bvh_state.first_node_index ||
            level_state.first_node_index > expected_node_end_index ||
            level_state.node_count != expected_node_end_index - level_state.first_node_index) {
            return false;
        }
        expected_node_end_index = level_state.first_node_index;
    }

    return expected_node_end_index == bvh_state.first_node_index;
}

bool garment_bvhs_cover_node_buffer(const std::array<GarmentBvhState, 2>& garment_bvhs,
                                    std::uint32_t total_count)
{
    std::array<const GarmentBvhState*, 2> ordered_bvh_states{&garment_bvhs[0], &garment_bvhs[1]};
    if (ordered_bvh_states[1]->first_node_index < ordered_bvh_states[0]->first_node_index) {
        std::swap(ordered_bvh_states[0], ordered_bvh_states[1]);
    }

    std::uint32_t expected_first_node_index = 0;
    for (const GarmentBvhState* bvh_state : ordered_bvh_states) {
        if (bvh_state->node_count == 0) {
            continue;
        }
        if (bvh_state->first_node_index != expected_first_node_index) {
            return false;
        }
        expected_first_node_index += bvh_state->node_count;
    }
    return expected_first_node_index == total_count;
}

}

void ClothBvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "cloth_bvh_bounds_update.comp",
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
    if (program_ == 0 ||
        !is_valid_motion_view(motion_view) ||
        !is_valid_cloth_mesh_topology_resource(topology) ||
        !is_valid_cloth_bvh_buffer_view(bvh_view) ||
        topology.vertex_count != motion_view.vertex_count ||
        !std::isfinite(bounds_margin) ||
        bounds_margin < 0.0f) {
        return false;
    }

    for (std::size_t layer = 0; layer < bvh_view.garment_bvhs->size(); ++layer) {
        const GarmentBvhState& bvh_state = (*bvh_view.garment_bvhs)[layer];
        const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
        if (garment_state.vertex_count == 0 && bvh_state.node_count == 0 && bvh_state.levels.empty()) {
            continue;
        }

        if (!is_valid_buffer_access(garment_state.vertex_start_index,
                                    garment_state.vertex_count,
                                    motion_view.vertex_count) ||
            !is_valid_buffer_access(bvh_state.first_node_index, bvh_state.node_count, bvh_view.node_count) ||
            !has_valid_bvh_levels(bvh_state)) {
            return false;
        }
    }

    return garment_bvhs_cover_node_buffer(*bvh_view.garment_bvhs, bvh_view.node_count);
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
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_triangle_bounds_binding,
                        bvh_view.triangle_bounds_buffer);
    gl.glProgramUniform1f(program_, bounds_margin_location_, bounds_margin);

    std::size_t level_count = 0;
    for (const GarmentBvhState& bvh_state : *bvh_view.garment_bvhs) {
        level_count = std::max(level_count, bvh_state.levels.size());
    }

    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        for (std::size_t layer = 0; layer < bvh_view.garment_bvhs->size(); ++layer) {
            const GarmentBvhState& bvh_state = (*bvh_view.garment_bvhs)[layer];
            if (level_index >= bvh_state.levels.size()) {
                continue;
            }

            const BvhLevelState& level_state = bvh_state.levels[level_index];

            gl.glProgramUniform1ui(program_, level_first_node_index_location_, level_state.first_node_index);
            gl.glProgramUniform1ui(program_, level_node_count_location_, level_state.node_count);
            gl.glDispatchCompute(compute_group_count(level_state.node_count, bvh_bounds_update_local_size),
                                 1,
                                 1);
        }

        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void ClothBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
}
