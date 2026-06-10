#include "gpu/scene/NormalUpdater.h"

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "utils/FileUtils.h"
#include "utils/ShaderUtils.h"

#include <iostream>

namespace {
// Triangle pass bindings
constexpr GLuint positions_binding = 0;
constexpr GLuint indices_binding = 1;
constexpr GLuint triangle_normals_binding = 2;

// Vertex pass bindings
constexpr GLuint vertex_pass_triangle_normals_binding = 0;
constexpr GLuint adjacent_triangle_offsets_binding = 1;
constexpr GLuint adjacent_triangle_indices_binding = 2;
constexpr GLuint vertex_normals_binding = 3;

// Dispatch constants
constexpr std::uint32_t normal_update_local_size = 128;
constexpr std::uint32_t triangle_normal_buffer_stride = 1;
constexpr std::uint32_t triangle_normal_buffer_offset = 0;
constexpr std::uint32_t triangle_geometry_face_normal_stride = 5;
constexpr std::uint32_t triangle_geometry_face_normal_offset = 3;

bool has_valid_vertex_normal_inputs(GLuint adjacent_triangle_offsets_buffer,
                                    GLuint adjacent_triangle_indices_buffer,
                                    GLuint vertex_normal_buffer,
                                    std::uint32_t vertex_count)
{
    return adjacent_triangle_offsets_buffer != 0 &&
           adjacent_triangle_indices_buffer != 0 &&
           vertex_normal_buffer != 0 &&
           vertex_count != 0;
}
}

bool NormalUpdater::is_initialized() const
{
    return triangle_program_ != 0 && vertex_program_ != 0;
}

bool NormalUpdater::initialize(const std::filesystem::path& triangle_normal_shader_path,
                               const std::filesystem::path& vertex_normal_shader_path,
                               QOpenGLFunctions_4_5_Core& gl)
{
    triangle_program_ = load_compute_program(triangle_normal_shader_path, gl);
    if (triangle_program_ == 0) {
        return false;
    }

    vertex_program_ = load_compute_program(vertex_normal_shader_path, gl);
    if (vertex_program_ == 0) {
        gl.glDeleteProgram(triangle_program_);
        triangle_program_ = 0;
        return false;
    }

    triangle_count_location_ = gl.glGetUniformLocation(triangle_program_, "uTriangleCount");
    position_component_offset_location_ = gl.glGetUniformLocation(triangle_program_, "uPositionComponentOffset");
    vertex_count_location_ = gl.glGetUniformLocation(vertex_program_, "uVertexCount");
    triangle_normal_stride_location_ = gl.glGetUniformLocation(vertex_program_, "uTriangleNormalStride");
    triangle_normal_offset_location_ = gl.glGetUniformLocation(vertex_program_, "uTriangleNormalOffset");
    return true;
}

void NormalUpdater::release(QOpenGLFunctions_4_5_Core& gl)
{
    if (vertex_program_ != 0) {
        gl.glDeleteProgram(vertex_program_);
    }
    if (triangle_program_ != 0) {
        gl.glDeleteProgram(triangle_program_);
    }

    triangle_program_ = 0;
    vertex_program_ = 0;
    triangle_count_location_ = -1;
    position_component_offset_location_ = -1;
    vertex_count_location_ = -1;
    triangle_normal_stride_location_ = -1;
    triangle_normal_offset_location_ = -1;
}

void NormalUpdater::update_cloth_normals(const ClothMeshTopologyResources& topology,
                                         const ClothNormalResources& normals,
                                         QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        topology.position_buffer == 0 ||
        topology.index_buffer == 0 ||
        topology.adjacent_triangle_offsets_buffer == 0 ||
        topology.adjacent_triangle_indices_buffer == 0 ||
        normals.triangle_normal_buffer == 0 ||
        normals.vertex_normal_buffer == 0 ||
        topology.vertex_count == 0 ||
        topology.triangle_count == 0) {
        return;
    }

    // triangle normal 계산
    gl.glUseProgram(triangle_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, positions_binding, topology.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, indices_binding, topology.index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_normals_binding, normals.triangle_normal_buffer);
    if (triangle_count_location_ >= 0) {
        gl.glProgramUniform1ui(triangle_program_, triangle_count_location_, topology.triangle_count);
    }
    if (position_component_offset_location_ >= 0) {
        gl.glProgramUniform1ui(triangle_program_, position_component_offset_location_, 0u);
    }
    gl.glDispatchCompute(compute_group_count(topology.triangle_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    update_vertex_normals(normals.triangle_normal_buffer,
                          topology.adjacent_triangle_offsets_buffer,
                          topology.adjacent_triangle_indices_buffer,
                          normals.vertex_normal_buffer,
                          topology.vertex_count,
                          triangle_normal_buffer_stride,
                          triangle_normal_buffer_offset,
                          gl);
}

void NormalUpdater::update_character_normals(const CharacterMeshTopologyResources& topology,
                                             const CharacterNormalResources& normals,
                                             QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        normals.triangle_geometry_buffer == 0 ||
        normals.triangle_count == 0 ||
        normals.triangle_count != topology.triangle_count ||
        !has_valid_vertex_normal_inputs(topology.adjacent_triangle_offsets_buffer,
                                        topology.adjacent_triangle_indices_buffer,
                                        normals.vertex_normal_buffer,
                                        topology.vertex_count)) {
        return;
    }

    update_vertex_normals(normals.triangle_geometry_buffer,
                          topology.adjacent_triangle_offsets_buffer,
                          topology.adjacent_triangle_indices_buffer,
                          normals.vertex_normal_buffer,
                          topology.vertex_count,
                          triangle_geometry_face_normal_stride,
                          triangle_geometry_face_normal_offset,
                          gl);
}

void NormalUpdater::update_vertex_normals(GLuint triangle_normal_source_buffer,
                                          GLuint adjacent_triangle_offsets_buffer,
                                          GLuint adjacent_triangle_indices_buffer,
                                          GLuint vertex_normal_buffer,
                                          std::uint32_t vertex_count,
                                          std::uint32_t triangle_normal_stride,
                                          std::uint32_t triangle_normal_offset,
                                          QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        triangle_normal_source_buffer == 0 ||
        !has_valid_vertex_normal_inputs(adjacent_triangle_offsets_buffer,
                                        adjacent_triangle_indices_buffer,
                                        vertex_normal_buffer,
                                        vertex_count)) {
        return;
    }

    // vertex normal 계산
    gl.glUseProgram(vertex_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_pass_triangle_normals_binding, triangle_normal_source_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacent_triangle_offsets_binding, adjacent_triangle_offsets_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacent_triangle_indices_binding, adjacent_triangle_indices_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_normals_binding, vertex_normal_buffer);
    if (vertex_count_location_ >= 0) {
        gl.glProgramUniform1ui(vertex_program_, vertex_count_location_, vertex_count);
    }
    if (triangle_normal_stride_location_ >= 0) {
        gl.glProgramUniform1ui(vertex_program_, triangle_normal_stride_location_, triangle_normal_stride);
    }
    if (triangle_normal_offset_location_ >= 0) {
        gl.glProgramUniform1ui(vertex_program_, triangle_normal_offset_location_, triangle_normal_offset);
    }
    gl.glDispatchCompute(compute_group_count(vertex_count, normal_update_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// shader loading //
GLuint NormalUpdater::load_compute_program(const std::filesystem::path& shader_path,
                                           QOpenGLFunctions_4_5_Core& gl) const
{
    const auto shader_source = read_text_file(shader_path);
    if (!shader_source) {
        return 0;
    }

    const GLuint shader = compile_compute_shader(shader_source->c_str(), gl);
    if (shader == 0) {
        return 0;
    }

    const GLuint program = gl.glCreateProgram();
    gl.glAttachShader(program, shader);
    gl.glLinkProgram(program);

    GLint success = 0;
    gl.glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Compute program link failed: " << log << '\n';
        gl.glDeleteShader(shader);
        gl.glDeleteProgram(program);
        return 0;
    }

    gl.glDeleteShader(shader);
    return program;
}

GLuint NormalUpdater::compile_compute_shader(const char* source, QOpenGLFunctions_4_5_Core& gl) const
{
    const GLuint shader = gl.glCreateShader(GL_COMPUTE_SHADER);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Compute shader compile failed: " << log << '\n';
        gl.glDeleteShader(shader);
        return 0;
    }

    return shader;
}
