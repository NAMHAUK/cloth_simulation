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

void NormalUpdater::update_normals(const NormalUpdateInputs& inputs, QOpenGLFunctions_4_5_Core& gl) const
{
    if (!is_initialized() ||
        inputs.position_buffer == 0 ||
        inputs.index_buffer == 0 ||
        inputs.adjacency_offset_buffer == 0 ||
        inputs.adjacency_triangle_buffer == 0 ||
        inputs.triangle_normal_buffer == 0 ||
        inputs.vertex_normal_buffer == 0 ||
        inputs.vertex_count == 0 ||
        inputs.triangle_count == 0) {
        return;
    }

    // triangle normal 계산
    gl.glUseProgram(triangle_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, positions_binding, inputs.position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, indices_binding, inputs.index_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, triangle_normals_binding, inputs.triangle_normal_buffer);
    if (triangle_count_location_ >= 0) {
        gl.glProgramUniform1ui(triangle_program_, triangle_count_location_, inputs.triangle_count);
    }
    if (position_component_offset_location_ >= 0) {
        gl.glProgramUniform1ui(triangle_program_, position_component_offset_location_, inputs.position_component_offset);
    }
    gl.glDispatchCompute(normal_update_group_count(inputs.triangle_count), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // vertex normal 계산
    gl.glUseProgram(vertex_program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_pass_triangle_normals_binding, inputs.triangle_normal_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacency_offsets_binding, inputs.adjacency_offset_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, adjacency_triangles_binding, inputs.adjacency_triangle_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vertex_normals_binding, inputs.vertex_normal_buffer);
    if (vertex_count_location_ >= 0) {
        gl.glProgramUniform1ui(vertex_program_, vertex_count_location_, inputs.vertex_count);
    }
    gl.glDispatchCompute(normal_update_group_count(inputs.vertex_count), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}


// adjacency data //
bool VertexTriangleAdjacency::is_valid(std::uint32_t vertex_count) const
{
    return triangle_count > 0 &&
           offsets.size() == static_cast<std::size_t>(vertex_count) + 1u &&
           !triangles.empty();
}

// 각 vertex가 어떤 triangle에 속해 있는지 triangle index 목록 생성
bool build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                     const std::vector<std::uint32_t>& triangle_indices,
                                     VertexTriangleAdjacency& adjacency)
{
    adjacency = {};

    if (vertex_count == 0 || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return false;
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / 3u);
    adjacency.offsets.resize(static_cast<std::size_t>(vertex_count) + 1u, 0);

    // vertex별 연결된 triangle 개수 계산
    for (std::uint32_t index : triangle_indices) {
        if (index >= vertex_count) {
            return false;
        }
        ++adjacency.offsets[static_cast<std::size_t>(index) + 1u];
    }

    // vertex별 triangle 수를 prefix sum으로 변환
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const std::size_t offset_index = static_cast<std::size_t>(vertex_index);
        const std::size_t next_offset_index = offset_index + 1u;
        adjacency.offsets[next_offset_index] += adjacency.offsets[offset_index];
    }

    adjacency.triangles.resize(adjacency.offsets.back(), 0);
    std::vector<std::uint32_t> write_offsets = adjacency.offsets;

    // vertex별 triangle index 목록 작성
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        for (std::uint32_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertex_index = triangle_indices[index_base + corner];
            const std::uint32_t write_index = write_offsets[vertex_index]++;
            adjacency.triangles[write_index] = triangle_index;
        }
    }

    adjacency.triangle_count = triangle_count;
    return adjacency.is_valid(vertex_count);
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
