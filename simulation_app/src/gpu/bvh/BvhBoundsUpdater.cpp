#include "gpu/bvh/BvhBoundsUpdater.h"

#include "gpu/scene/SimulationGpuView.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::uint32_t bvh_bounds_update_local_size = 128;

std::pair<std::uint32_t, std::uint32_t> valid_or_empty_level(const std::vector<std::uint32_t>& level_offsets,
                                                             std::size_t level_index)
{
    if (level_index + 1u >= level_offsets.size()) {
        return {};
    }

    const std::size_t offset_index = level_offsets.size() - 1u - level_index;
    const std::uint32_t first_node_index = level_offsets[offset_index - 1u];
    const std::uint32_t node_end_index = level_offsets[offset_index];
    return {first_node_index, node_end_index - first_node_index};
}
}

// Initialization

void BvhBoundsUpdater::initialize(const std::filesystem::path& shader_dir,
                                  float body_detection_distance,
                                  QOpenGLFunctions_4_5_Core& gl)
{
    body_program_ = load_compute_program(shader_dir / "bvh" / "body_bounds_update.comp", gl);
    cloth_program_ = load_compute_program(shader_dir / "bvh" / "cloth_bounds_update.comp", gl);

    body_triangle_first_node_index_location_ =
        require_uniform_location(body_program_, "uTriangleFirstNodeIndex", gl);
    body_triangle_node_count_location_ = require_uniform_location(body_program_, "uTriangleNodeCount", gl);
    body_vertex_first_node_index_location_ =
        require_uniform_location(body_program_, "uVertexFirstNodeIndex", gl);
    body_vertex_node_count_location_ = require_uniform_location(body_program_, "uVertexNodeCount", gl);
    body_edge_first_node_index_location_ = require_uniform_location(body_program_, "uEdgeFirstNodeIndex", gl);
    body_edge_node_count_location_ = require_uniform_location(body_program_, "uEdgeNodeCount", gl);
    body_detection_distance_location_ = require_uniform_location(body_program_, "uDetectionDistance", gl);
    cloth_level_first_node_index_location_ =
        require_uniform_location(cloth_program_, "uLevelFirstNodeIndex", gl);
    cloth_level_node_count_location_ = require_uniform_location(cloth_program_, "uLevelNodeCount", gl);
    cloth_bounds_margin_location_ = require_uniform_location(cloth_program_, "uBoundsMargin", gl);

    gl.glProgramUniform1f(body_program_, body_detection_distance_location_, body_detection_distance);
}

void BvhBoundsUpdater::set_body_level_offsets(const std::vector<std::uint32_t>& triangle_level_offsets,
                                              const std::vector<std::uint32_t>& vertex_level_offsets,
                                              const std::vector<std::uint32_t>& edge_level_offsets)
{
    body_triangle_level_offsets_ = triangle_level_offsets;
    body_vertex_level_offsets_ = vertex_level_offsets;
    body_edge_level_offsets_ = edge_level_offsets;
}

// Bounds update

void BvhBoundsUpdater::update_body_bvh(const SimulationGpuView&, QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(body_program_);

    const std::size_t level_count = std::max({body_triangle_level_offsets_.size(),
                                              body_vertex_level_offsets_.size(),
                                              body_edge_level_offsets_.size()}) -
                                    1u;
    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        const auto [triangle_first_node_index, triangle_node_count] =
            valid_or_empty_level(body_triangle_level_offsets_, level_index);
        const auto [vertex_first_node_index, vertex_node_count] =
            valid_or_empty_level(body_vertex_level_offsets_, level_index);
        const auto [edge_first_node_index, edge_node_count] =
            valid_or_empty_level(body_edge_level_offsets_, level_index);

        const std::uint32_t dispatch_node_count =
            std::max({triangle_node_count, vertex_node_count, edge_node_count});
        if (dispatch_node_count == 0) {
            continue;
        }

        gl.glProgramUniform1ui(body_program_,
                               body_triangle_first_node_index_location_,
                               triangle_first_node_index);
        gl.glProgramUniform1ui(body_program_, body_triangle_node_count_location_, triangle_node_count);
        gl.glProgramUniform1ui(body_program_,
                               body_vertex_first_node_index_location_,
                               vertex_first_node_index);
        gl.glProgramUniform1ui(body_program_, body_vertex_node_count_location_, vertex_node_count);
        gl.glProgramUniform1ui(body_program_, body_edge_first_node_index_location_, edge_first_node_index);
        gl.glProgramUniform1ui(body_program_, body_edge_node_count_location_, edge_node_count);
        gl.glDispatchCompute(compute_group_count(dispatch_node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void BvhBoundsUpdater::update_cloth_bvh(const SimulationGpuView& views,
                                        float bounds_margin,
                                        QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_update_cloth(views, bounds_margin));

    gl.glUseProgram(cloth_program_);
    gl.glProgramUniform1f(cloth_program_, cloth_bounds_margin_location_, bounds_margin);

    std::size_t level_count = 0;
    for (const GarmentBufferState& garment_state : views.garment_buffer_states) {
        if (!garment_state.bvh_level_offsets.empty()) {
            level_count = std::max(level_count, garment_state.bvh_level_offsets.size() - 1u);
        }
    }

    for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
        for (const GarmentBufferState& garment_state : views.garment_buffer_states) {
            const auto [first_node_index, node_count] =
                valid_or_empty_level(garment_state.bvh_level_offsets, level_index);
            if (node_count == 0) {
                continue;
            }

            gl.glProgramUniform1ui(cloth_program_, cloth_level_first_node_index_location_, first_node_index);
            gl.glProgramUniform1ui(cloth_program_, cloth_level_node_count_location_, node_count);
            gl.glDispatchCompute(compute_group_count(node_count, bvh_bounds_update_local_size), 1, 1);
        }

        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

// Validation

bool BvhBoundsUpdater::can_update_cloth(const SimulationGpuView& views, float bounds_margin) const
{
    return cloth_program_ != 0 &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_cloth_mesh_topology_resource(views.cloth_topology) &&
           is_valid_bvh_buffer_view(views.cloth_bvh) &&
           views.cloth_topology.vertex_count == views.cloth_motion.vertex_count &&
           std::isfinite(bounds_margin) &&
           bounds_margin >= 0.0f;
}

// Release

void BvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_program_);
    gl.glDeleteProgram(body_program_);

    body_program_ = 0;
    body_triangle_first_node_index_location_ = -1;
    body_triangle_node_count_location_ = -1;
    body_vertex_first_node_index_location_ = -1;
    body_vertex_node_count_location_ = -1;
    body_edge_first_node_index_location_ = -1;
    body_edge_node_count_location_ = -1;
    body_detection_distance_location_ = -1;
    body_triangle_level_offsets_.clear();
    body_vertex_level_offsets_.clear();
    body_edge_level_offsets_.clear();
    cloth_program_ = 0;
    cloth_level_first_node_index_location_ = -1;
    cloth_level_node_count_location_ = -1;
    cloth_bounds_margin_location_ = -1;
}
