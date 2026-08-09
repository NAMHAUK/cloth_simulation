#include "gpu/bvh/ClothBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
constexpr GLuint cloth_current_positions_binding = 0;
constexpr GLuint cloth_previous_positions_binding = 1;
constexpr GLuint collision_triangle_indices_binding = 2;
constexpr GLuint cloth_bvh_nodes_binding = 3;
constexpr GLuint cloth_triangle_bounds_binding = 4;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;

bool is_valid_range(std::uint32_t offset, std::uint32_t count, std::uint32_t total_count)
{
    return count != 0 && offset <= total_count && count <= total_count - offset;
}

bool has_valid_node_level_ranges(const GarmentBvhLayout& layout)
{
    if (layout.node_ranges_by_level.empty()) {
        return false;
    }

    std::uint32_t expected_range_end = layout.range.bvh_nodes.count;
    for (const BvhNodeRange& range : layout.node_ranges_by_level) {
        if (range.node_count == 0 ||
            range.first_node > expected_range_end ||
            range.node_count != expected_range_end - range.first_node) {
            return false;
        }
        expected_range_end = range.first_node;
    }

    return expected_range_end == 0;
}

const GarmentBufferRanges* find_garment_buffer_ranges(
    const std::vector<GarmentBufferRanges>& garment_buffer_ranges,
    GarmentLayer layer)
{
    const auto iter =
        std::find_if(garment_buffer_ranges.begin(),
                     garment_buffer_ranges.end(),
                     [layer](const GarmentBufferRanges& range) { return range.layer == layer; });
    return iter == garment_buffer_ranges.end() ? nullptr : &(*iter);
}
}

bool ClothBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path,
                                       QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Cloth BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    vertex_offset_location_ = gl.glGetUniformLocation(program_, "uVertexOffset");
    collision_triangle_offset_location_ = gl.glGetUniformLocation(program_, "uCollisionTriangleOffset");
    bvh_node_offset_location_ = gl.glGetUniformLocation(program_, "uBvhNodeOffset");
    level_first_node_location_ = gl.glGetUniformLocation(program_, "uLevelFirstNode");
    level_node_count_location_ = gl.glGetUniformLocation(program_, "uLevelNodeCount");
    bounds_margin_location_ = gl.glGetUniformLocation(program_, "uBoundsMargin");

    if (std::min({vertex_offset_location_,
                  collision_triangle_offset_location_,
                  bvh_node_offset_location_,
                  level_first_node_location_,
                  level_node_count_location_,
                  bounds_margin_location_}) < 0) {
        std::cerr << "Cloth BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

bool ClothBvhBoundsUpdater::can_update(const ClothMotionBufferView& motion_view,
                                       const ClothBvhBufferView& bvh_view,
                                       const std::vector<GarmentBufferRanges>& garment_buffer_ranges,
                                       float bounds_margin) const
{
    if (program_ == 0 ||
        !is_valid_motion_view(motion_view) ||
        !is_valid_cloth_bvh_buffer_view(bvh_view) ||
        !std::isfinite(bounds_margin) ||
        bounds_margin < 0.0f) {
        return false;
    }

    std::uint32_t expected_triangle_offset = 0;
    std::uint32_t expected_node_offset = 0;
    for (const GarmentBvhLayout& layout : *bvh_view.garment_layouts) {
        const GarmentBvhRange& bvh_range = layout.range;
        const GarmentBufferRanges* garment_range =
            find_garment_buffer_ranges(garment_buffer_ranges, bvh_range.layer);
        if (garment_range == nullptr ||
            !is_valid_range(garment_range->vertex_offset,
                            garment_range->vertex_count,
                            motion_view.vertex_count) ||
            !is_valid_range(bvh_range.collision_triangles.offset,
                            bvh_range.collision_triangles.count,
                            bvh_view.triangle_count) ||
            !is_valid_range(bvh_range.bvh_nodes.offset, bvh_range.bvh_nodes.count, bvh_view.node_count) ||
            bvh_range.collision_triangles.offset != expected_triangle_offset ||
            bvh_range.bvh_nodes.offset != expected_node_offset ||
            bvh_range.collision_triangles.count != garment_range->triangle_count ||
            !has_valid_node_level_ranges(layout)) {
            return false;
        }

        expected_triangle_offset += bvh_range.collision_triangles.count;
        expected_node_offset += bvh_range.bvh_nodes.count;
    }

    return expected_triangle_offset == bvh_view.triangle_count && expected_node_offset == bvh_view.node_count;
}

void ClothBvhBoundsUpdater::update(const ClothMotionBufferView& motion_view,
                                   const ClothBvhBufferView& bvh_view,
                                   const std::vector<GarmentBufferRanges>& garment_buffer_ranges,
                                   float bounds_margin,
                                   QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_update(motion_view, bvh_view, garment_buffer_ranges, bounds_margin));

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        collision_triangle_indices_binding,
                        bvh_view.collision_triangle_index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, cloth_bvh_nodes_binding, bvh_view.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_triangle_bounds_binding,
                        bvh_view.triangle_bounds_buffer);
    gl.glProgramUniform1f(program_, bounds_margin_location_, bounds_margin);

    std::size_t level_count = 0;
    for (const GarmentBvhLayout& layout : *bvh_view.garment_layouts) {
        level_count = std::max(level_count, layout.node_ranges_by_level.size());
    }

    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        for (const GarmentBvhLayout& layout : *bvh_view.garment_layouts) {
            if (level_index >= layout.node_ranges_by_level.size()) {
                continue;
            }

            const GarmentBvhRange& bvh_range = layout.range;
            const GarmentBufferRanges* garment_range =
                find_garment_buffer_ranges(garment_buffer_ranges, bvh_range.layer);
            const BvhNodeRange& level_range = layout.node_ranges_by_level[level_index];

            gl.glProgramUniform1ui(program_, vertex_offset_location_, garment_range->vertex_offset);
            gl.glProgramUniform1ui(program_,
                                   collision_triangle_offset_location_,
                                   bvh_range.collision_triangles.offset);
            gl.glProgramUniform1ui(program_, bvh_node_offset_location_, bvh_range.bvh_nodes.offset);
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
