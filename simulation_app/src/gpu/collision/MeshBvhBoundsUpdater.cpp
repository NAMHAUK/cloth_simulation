#include "gpu/collision/MeshBvhBoundsUpdater.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint character_triangle_geometry_binding = 0;
constexpr GLuint character_bvh_node_binding = 1;
constexpr std::uint32_t bvh_bounds_update_local_size = 128;
}

bool MeshBvhBoundsUpdater::is_initialized() const
{
    return program_ != 0;
}

bool MeshBvhBoundsUpdater::can_update(const TriangleGeometryResources& character_geometry,
                                      const MeshBvhResources& character_bvh,
                                      const std::vector<BvhNodeRange>& node_ranges_by_level,
                                      float collision_thickness) const
{
    return is_initialized() &&
           is_valid_triangle_geometry_resource(character_geometry) &&
           is_valid_mesh_bvh_resource(character_bvh) &&
           !node_ranges_by_level.empty() &&
           collision_thickness > 0.0f;
}

bool MeshBvhBoundsUpdater::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character BVH bounds update", gl);
    if (program_ == 0) {
        return false;
    }

    first_node_location_ = gl.glGetUniformLocation(program_, "uFirstNode");
    node_count_location_ = gl.glGetUniformLocation(program_, "uNodeCount");
    collision_thickness_location_ = gl.glGetUniformLocation(program_, "uCollisionThickness");

    if (first_node_location_ < 0 || node_count_location_ < 0 || collision_thickness_location_ < 0) {
        std::cerr << "Character BVH bounds update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void MeshBvhBoundsUpdater::update(const TriangleGeometryResources& character_geometry,
                                  const MeshBvhResources& character_bvh,
                                  const std::vector<BvhNodeRange>& node_ranges_by_level,
                                  float collision_thickness,
                                  QOpenGLFunctions_4_5_Core& gl) const
{
    if (!can_update(character_geometry, character_bvh, node_ranges_by_level, collision_thickness)) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_triangle_geometry_binding, character_geometry.triangle_geometry_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, character_bvh_node_binding, character_bvh.node_buffer);
    gl.glProgramUniform1f(program_, collision_thickness_location_, collision_thickness);

    for (const BvhNodeRange& range : node_ranges_by_level) {
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

void MeshBvhBoundsUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    first_node_location_ = -1;
    node_count_location_ = -1;
    collision_thickness_location_ = -1;
}
