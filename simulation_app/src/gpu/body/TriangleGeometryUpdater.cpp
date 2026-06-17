#include "gpu/body/TriangleGeometryUpdater.h"

#include "gpu/body/CharacterGpuDataTypes.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
constexpr GLuint positions_binding = 0;
constexpr GLuint indices_binding = 1;
constexpr GLuint triangle_geometry_binding = 2;
constexpr std::uint32_t triangle_geometry_local_size = 128;

bool is_valid_update_input(const CharacterMeshTopologyResources& topology,
                           const TriangleGeometryResources& triangle_geometry)
{
    return topology.position_buffer != 0 &&
           topology.index_buffer != 0 &&
           topology.triangle_count != 0 &&
           triangle_geometry.triangle_geometry_buffer != 0 &&
           triangle_geometry.triangle_count != 0 &&
           triangle_geometry.triangle_count == topology.triangle_count;
}
}

bool TriangleGeometryUpdater::is_initialized() const
{
    return program_ != 0;
}

bool TriangleGeometryUpdater::initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_path, "Character triangle geometry update", gl);
    if (program_ == 0) {
        return false;
    }

    triangle_count_location_ = gl.glGetUniformLocation(program_, "uTriangleCount");
    current_frame_begin_location_ = gl.glGetUniformLocation(program_, "uCurrentFramePositionBeginIndex");
    next_frame_begin_location_ = gl.glGetUniformLocation(program_, "uNextFramePositionBeginIndex");
    frame_alpha_location_ = gl.glGetUniformLocation(program_, "uFrameAlpha");
    if (triangle_count_location_ < 0 ||current_frame_begin_location_ < 0 ||next_frame_begin_location_ < 0 ||frame_alpha_location_ < 0) {
        std::cerr << "Character triangle geometry update compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

void TriangleGeometryUpdater::update(const CharacterMeshTopologyResources& topology,
                                     const TriangleGeometryResources& triangle_geometry,
                                     std::uint32_t current_frame_position_begin_index,
                                     std::uint32_t next_frame_position_begin_index,
                                     float frame_alpha,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() || !is_valid_update_input(topology, triangle_geometry)) {
        return;
    }

    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, positions_binding, topology.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, indices_binding, topology.index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_geometry_binding, triangle_geometry.triangle_geometry_buffer);
    gl.glProgramUniform1ui(program_, triangle_count_location_, topology.triangle_count);
    gl.glProgramUniform1ui(program_, current_frame_begin_location_, current_frame_position_begin_index);
    gl.glProgramUniform1ui(program_, next_frame_begin_location_, next_frame_position_begin_index);
    gl.glProgramUniform1f(program_, frame_alpha_location_, frame_alpha);

    gl.glDispatchCompute(compute_group_count(topology.triangle_count, triangle_geometry_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void TriangleGeometryUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (program_ != 0) {
        gl.glDeleteProgram(program_);
    }

    program_ = 0;
    triangle_count_location_ = -1;
    current_frame_begin_location_ = -1;
    next_frame_begin_location_ = -1;
    frame_alpha_location_ = -1;
}
