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

bool is_valid_range(std::uint32_t offset, std::uint32_t count, std::uint32_t total_count)
{
    return count != 0 && offset <= total_count && count <= total_count - offset;
}

bool has_valid_node_level_ranges(const GarmentBvhRanges& ranges)
{
    if (ranges.node_ranges_by_level.empty()) {
        return false;
    }

    std::uint32_t expected_range_end = ranges.nodes.offset + ranges.nodes.count;
    for (const BvhNodeRange& range : ranges.node_ranges_by_level) {
        if (range.node_count == 0 ||
            range.first_node < ranges.nodes.offset ||
            range.first_node > expected_range_end ||
            range.node_count != expected_range_end - range.first_node) {
            return false;
        }
        expected_range_end = range.first_node;
    }

    return expected_range_end == ranges.nodes.offset;
}

bool ranges_cover_buffer(std::array<BvhBufferRange, 2> ranges, std::uint32_t total_count)
{
    if (ranges[1].offset < ranges[0].offset) {
        std::swap(ranges[0], ranges[1]);
    }

    std::uint32_t expected_offset = 0;
    for (const BvhBufferRange& range : ranges) {
        if (range.count == 0) {
            continue;
        }
        if (range.offset != expected_offset) {
            return false;
        }
        expected_offset += range.count;
    }
    return expected_offset == total_count;
}

}

void ClothBvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "cloth_bvh_bounds_update.comp",
                                    "Cloth BVH bounds update",
                                    gl);
    level_first_node_location_ = gl.glGetUniformLocation(program_, "uLevelFirstNode");
    level_node_count_location_ = gl.glGetUniformLocation(program_, "uLevelNodeCount");
    bounds_margin_location_ = gl.glGetUniformLocation(program_, "uBoundsMargin");

    if (std::min({level_first_node_location_, level_node_count_location_, bounds_margin_location_}) < 0) {
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
        topology.triangle_count != bvh_view.triangle_count ||
        !std::isfinite(bounds_margin) ||
        bounds_margin < 0.0f) {
        return false;
    }

    std::uint32_t garment_count = 0;
    for (std::size_t layer = 0; layer < bvh_view.garment_ranges->size(); ++layer) {
        const GarmentBvhRanges& ranges = (*bvh_view.garment_ranges)[layer];
        const ElementRange& vertex_range = views.garment_vertex_ranges[layer];
        if (vertex_range.count == 0 &&
            ranges.collision_triangles.count == 0 &&
            ranges.nodes.count == 0 &&
            ranges.node_ranges_by_level.empty()) {
            continue;
        }

        if (!is_valid_range(vertex_range.offset, vertex_range.count, motion_view.vertex_count) ||
            !is_valid_range(ranges.collision_triangles.offset,
                            ranges.collision_triangles.count,
                            bvh_view.triangle_count) ||
            !is_valid_range(ranges.nodes.offset, ranges.nodes.count, bvh_view.node_count) ||
            !has_valid_node_level_ranges(ranges)) {
            return false;
        }
        ++garment_count;
    }

    const auto& garment_ranges = *bvh_view.garment_ranges;
    return garment_count == bvh_view.garment_count &&
           ranges_cover_buffer({garment_ranges[0].collision_triangles, garment_ranges[1].collision_triangles},
                               bvh_view.triangle_count) &&
           ranges_cover_buffer({garment_ranges[0].nodes, garment_ranges[1].nodes}, bvh_view.node_count);
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
    for (const GarmentBvhRanges& ranges : *bvh_view.garment_ranges) {
        level_count = std::max(level_count, ranges.node_ranges_by_level.size());
    }

    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        for (std::size_t layer = 0; layer < bvh_view.garment_ranges->size(); ++layer) {
            const GarmentBvhRanges& ranges = (*bvh_view.garment_ranges)[layer];
            if (level_index >= ranges.node_ranges_by_level.size()) {
                continue;
            }

            const BvhNodeRange& level_range = ranges.node_ranges_by_level[level_index];

            gl.glProgramUniform1ui(program_, level_first_node_location_, level_range.first_node);
            gl.glProgramUniform1ui(program_, level_node_count_location_, level_range.node_count);
            gl.glDispatchCompute(compute_group_count(level_range.node_count, bvh_bounds_update_local_size),
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
