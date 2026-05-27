#include "gpu/scene/NormalUpdater.h"

#include "support/FileUtils.h"

#include <iostream>

namespace {
// Triangle pass bindings
constexpr GLuint positions_binding = 0;
constexpr GLuint indices_binding = 1;
constexpr GLuint triangle_normals_binding = 2;

// Vertex pass bindings
constexpr GLuint vertex_pass_triangle_normals_binding = 0;
constexpr GLuint adjacency_offsets_binding = 1;
constexpr GLuint adjacency_triangles_binding = 2;
constexpr GLuint vertex_normals_binding = 3;

// Dispatch constants
constexpr std::uint32_t normal_update_local_size = 128;

// Shader paths
const std::filesystem::path triangle_normal_shader_path =
    std::filesystem::path(PROJECT_ROOT_DIR) / "simulation_app" / "shaders" / "triangle_normal.comp";

const std::filesystem::path vertex_normal_shader_path =
    std::filesystem::path(PROJECT_ROOT_DIR) / "simulation_app" / "shaders" / "vertex_normal.comp";

// Dispatch helpers
GLuint normal_update_group_count(std::uint32_t item_count)
{
    return static_cast<GLuint>((item_count + normal_update_local_size - 1u) / normal_update_local_size);
}
}

bool NormalUpdater::is_initialized() const
{
    return triangle_program_ != 0 && vertex_program_ != 0;
}

bool NormalUpdater::initialize(QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint next_triangle_program = load_compute_program(triangle_normal_shader_path, gl);
    if (next_triangle_program == 0) {
        return false;
    }

    const GLuint next_vertex_program = load_compute_program(vertex_normal_shader_path, gl);
    if (next_vertex_program == 0) {
        gl.glDeleteProgram(next_triangle_program);
        return false;
    }

    release(gl);
    triangle_program_ = next_triangle_program;
    vertex_program_ = next_vertex_program;
    triangle_count_location_ = gl.glGetUniformLocation(triangle_program_, "uTriangleCount");
    position_component_offset_location_ = gl.glGetUniformLocation(triangle_program_, "uPositionComponentOffset");
    vertex_count_location_ = gl.glGetUniformLocation(vertex_program_, "uVertexCount");
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
}

void NormalUpdater::update_normals(const MeshTopologyResources& topology,
                                   const MeshNormalResources& normals,
                                   QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        topology.position_buffer == 0 ||
        topology.index_buffer == 0 ||
        topology.adjacency_offset_buffer == 0 ||
        topology.adjacency_triangle_buffer == 0 ||
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
        gl.glProgramUniform1ui(triangle_program_, position_component_offset_location_, topology.position_component_offset);
    }
    gl.glDispatchCompute(normal_update_group_count(topology.triangle_count), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // vertex normal 계산
    gl.glUseProgram(vertex_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_pass_triangle_normals_binding, normals.triangle_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacency_offsets_binding, topology.adjacency_offset_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacency_triangles_binding, topology.adjacency_triangle_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_normals_binding, normals.vertex_normal_buffer);
    if (vertex_count_location_ >= 0) {
        gl.glProgramUniform1ui(vertex_program_, vertex_count_location_, topology.vertex_count);
    }
    gl.glDispatchCompute(normal_update_group_count(topology.vertex_count), 1, 1);
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
