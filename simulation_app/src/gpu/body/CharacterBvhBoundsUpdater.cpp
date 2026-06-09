#include "gpu/body/CharacterBvhBoundsUpdater.h"

#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint character_triangle_geometry_binding = 0;
constexpr GLuint character_bvh_node_binding = 1;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;

bool is_valid_character_geometry(const CharacterTriangleGeometryResources& character_geometry)
{
    return character_geometry.triangle_geometry_buffer != 0 &&
           character_geometry.triangle_count != 0;
}

bool is_valid_character_bvh(const CharacterBvhResources& character_bvh)
{
    return character_bvh.node_buffer != 0 &&
           character_bvh.node_count != 0 &&
           character_bvh.root_node_index < character_bvh.node_count;
}
}

bool CharacterBvhBoundsUpdater::is_initialized() const
{
    return program_ != 0;
}

bool CharacterBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    first_node_location_ = gl.glGetUniformLocation(program_, "uFirstNode");
    node_count_location_ = gl.glGetUniformLocation(program_, "uNodeCount");

    if (first_node_location_ < 0 || node_count_location_ < 0) {
        std::cerr << "Character BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void CharacterBvhBoundsUpdater::update(const CharacterTriangleGeometryResources& character_geometry,
                                       const CharacterBvhResources& character_bvh,
                                       const std::vector<BvhBoundsUpdateLevelRange>& level_ranges,
                                       QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        !is_valid_character_geometry(character_geometry) ||
        !is_valid_character_bvh(character_bvh) ||
        level_ranges.empty()) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);

    for (const BvhBoundsUpdateLevelRange& range : level_ranges) {
        if (range.node_count == 0 ||
            range.first_node >= character_bvh.node_count ||
            range.first_node + range.node_count > character_bvh.node_count) {
            continue;
        }

        gl.glProgramUniform1ui(program_, first_node_location_, range.first_node);
        gl.glProgramUniform1ui(program_, node_count_location_, range.node_count);
        gl.glDispatchCompute(compute_group_count(range.node_count, bvh_bounds_update_local_size), 1, 1);
        gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void CharacterBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    first_node_location_ = -1;
    node_count_location_ = -1;
}
