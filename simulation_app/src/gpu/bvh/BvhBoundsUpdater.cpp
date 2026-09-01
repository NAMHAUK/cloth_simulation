#include "gpu/bvh/BvhBoundsUpdater.h"

#include "gpu/cloth/ClothGpuState.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;

struct LevelRange final
{
    std::uint32_t first_node_index = 0;
    std::uint32_t node_count = 0;
};

LevelRange level_range(const std::vector<std::uint32_t>& level_offsets, std::size_t level_index)
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
    // body bvh program
    body_program_ = load_compute_program(shader_dir / "bvh" / "body_bounds_update.comp", gl);
    body_triangle_first_node_index_loc_ =
        require_uniform_location(body_program_, "uTriangleFirstNodeIndex", gl);
    body_triangle_node_count_loc_ = require_uniform_location(body_program_, "uTriangleNodeCount", gl);
    body_vertex_first_node_index_loc_ = require_uniform_location(body_program_, "uVertexFirstNodeIndex", gl);
    body_vertex_node_count_loc_ = require_uniform_location(body_program_, "uVertexNodeCount", gl);
    body_edge_first_node_index_loc_ = require_uniform_location(body_program_, "uEdgeFirstNodeIndex", gl);
    body_edge_node_count_loc_ = require_uniform_location(body_program_, "uEdgeNodeCount", gl);
    const GLint detection_distance_loc = require_uniform_location(body_program_, "uDetectionDistance", gl);
    gl.glProgramUniform1f(body_program_, detection_distance_loc, body_detection_distance);

    // cloth bvh program
    cloth_program_ = load_compute_program(shader_dir / "bvh" / "cloth_bounds_update.comp", gl);
    cloth_level_first_node_index_loc_ = require_uniform_location(cloth_program_, "uLevelFirstNodeIndex", gl);
    cloth_level_node_count_loc_ = require_uniform_location(cloth_program_, "uLevelNodeCount", gl);
    cloth_bounds_margin_loc_ = require_uniform_location(cloth_program_, "uBoundsMargin", gl);
}

void BvhBoundsUpdater::set_body_level_offsets(const std::vector<std::uint32_t>& triangle_level_offsets,
                                              const std::vector<std::uint32_t>& vertex_level_offsets,
                                              const std::vector<std::uint32_t>& edge_level_offsets)
{
    body_triangle_level_offsets_ = triangle_level_offsets;
    body_vertex_level_offsets_ = vertex_level_offsets;
    body_edge_level_offsets_ = edge_level_offsets;

    const std::size_t max_level_offset_count = std::max({body_triangle_level_offsets_.size(),
                                                         body_vertex_level_offsets_.size(),
                                                         body_edge_level_offsets_.size()});
    body_bvh_level_count_ = max_level_offset_count - 1u;
}

// Bounds update

void BvhBoundsUpdater::update_body_bvh(QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(body_program_);

    for (std::size_t level_index = 0; level_index < body_bvh_level_count_; ++level_index) {
        const auto triangle_level = level_range(body_triangle_level_offsets_, level_index);
        const auto vertex_level = level_range(body_vertex_level_offsets_, level_index);
        const auto edge_level = level_range(body_edge_level_offsets_, level_index);

        const std::uint32_t dispatch_node_count =
            std::max({triangle_level.node_count, vertex_level.node_count, edge_level.node_count});

        gl.glProgramUniform1ui(body_program_,
                               body_triangle_first_node_index_loc_,
                               triangle_level.first_node_index);
        gl.glProgramUniform1ui(body_program_, body_triangle_node_count_loc_, triangle_level.node_count);
        gl.glProgramUniform1ui(body_program_,
                               body_vertex_first_node_index_loc_,
                               vertex_level.first_node_index);
        gl.glProgramUniform1ui(body_program_, body_vertex_node_count_loc_, vertex_level.node_count);
        gl.glProgramUniform1ui(body_program_, body_edge_first_node_index_loc_, edge_level.first_node_index);
        gl.glProgramUniform1ui(body_program_, body_edge_node_count_loc_, edge_level.node_count);
        gl.glDispatchCompute(compute_group_count(dispatch_node_count, local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void BvhBoundsUpdater::update_cloth_bvh(const ClothGpuState& cloth_state,
                                        float bounds_margin,
                                        QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& garment_states = cloth_state.garment_states();

    gl.glUseProgram(cloth_program_);
    gl.glProgramUniform1f(cloth_program_, cloth_bounds_margin_loc_, bounds_margin);

    for (std::size_t level_index = 0; level_index < cloth_state.max_bvh_level_count(); ++level_index) {
        for (const GarmentBufferState& garment_state : garment_states) {
            const auto level = level_range(garment_state.bvh_level_offsets, level_index);

            gl.glProgramUniform1ui(cloth_program_, cloth_level_first_node_index_loc_, level.first_node_index);
            gl.glProgramUniform1ui(cloth_program_, cloth_level_node_count_loc_, level.node_count);
            gl.glDispatchCompute(compute_group_count(level.node_count, local_size), 1, 1);
        }

        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

// Release

void BvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_program_);
    gl.glDeleteProgram(body_program_);

    body_program_ = 0;
    body_triangle_first_node_index_loc_ = -1;
    body_triangle_node_count_loc_ = -1;
    body_vertex_first_node_index_loc_ = -1;
    body_vertex_node_count_loc_ = -1;
    body_edge_first_node_index_loc_ = -1;
    body_edge_node_count_loc_ = -1;
    body_bvh_level_count_ = 0;
    body_triangle_level_offsets_.clear();
    body_vertex_level_offsets_.clear();
    body_edge_level_offsets_.clear();
    cloth_program_ = 0;
    cloth_level_first_node_index_loc_ = -1;
    cloth_level_node_count_loc_ = -1;
    cloth_bounds_margin_loc_ = -1;
}
