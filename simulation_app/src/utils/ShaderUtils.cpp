#include "utils/ShaderUtils.h"

#include "utils/FileUtils.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

GLuint compile_compute_shader(const char* source,
                              const std::filesystem::path& shader_path,
                              QOpenGLFunctions_4_5_Core& gl)
{
    const GLuint shader = gl.glCreateShader(GL_COMPUTE_SHADER);
    gl.glShaderSource(shader, 1, &source, nullptr);
    gl.glCompileShader(shader);

    GLint success = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        gl.glDeleteShader(shader);
        throw std::runtime_error("Failed to compile compute shader: " + shader_path.string() + "\n" + log);
    }

    return shader;
}

std::optional<std::filesystem::path> parse_shader_include(const std::string& line)
{
    constexpr std::string_view include_prefix = "#include \"";
    const std::size_t directive_start = line.find_first_not_of(" \t");
    if (directive_start == std::string::npos ||
        line.compare(directive_start, include_prefix.size(), include_prefix) != 0) {
        return std::nullopt;
    }

    const std::size_t path_start = directive_start + include_prefix.size();
    const std::size_t path_end = line.find('"', path_start);
    if (path_end == std::string::npos) {
        return std::nullopt;
    }

    return std::filesystem::path{line.substr(path_start, path_end - path_start)};
}

std::string load_shader_source(const std::filesystem::path& shader_path,
                               std::vector<std::filesystem::path> include_stack)
{
    const std::filesystem::path normalized_path = std::filesystem::absolute(shader_path).lexically_normal();
    if (std::find(include_stack.begin(), include_stack.end(), normalized_path) != include_stack.end()) {
        throw std::runtime_error("Shader include cycle detected: " + normalized_path.string());
    }

    const auto source = read_text_file(normalized_path);
    if (!source) {
        throw std::runtime_error("Failed to load shader source: " + normalized_path.string());
    }

    include_stack.push_back(normalized_path);

    std::istringstream input(*source);
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        if (const auto include_path = parse_shader_include(line)) {
            const auto include_source =
                load_shader_source(normalized_path.parent_path() / *include_path, include_stack);
            output << include_source;
            if (include_source.empty() || include_source.back() != '\n') {
                output << '\n';
            }
            continue;
        }

        output << line << '\n';
    }

    return output.str();
}

}

GLuint load_compute_program(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl)
{
    const auto shader_source = load_shader_source(shader_path, {});
    const GLuint shader = compile_compute_shader(shader_source.c_str(), shader_path, gl);
    const GLuint program = gl.glCreateProgram();
    gl.glAttachShader(program, shader);
    gl.glLinkProgram(program);

    GLint linked = 0;
    gl.glGetProgramiv(program, GL_LINK_STATUS, &linked);
    gl.glDeleteShader(shader);
    if (!linked) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        gl.glDeleteProgram(program);
        throw std::runtime_error("Failed to link compute program: " + shader_path.string() + "\n" + log);
    }

    return program;
}

GLint require_uniform_location(GLuint program, const char* name, QOpenGLFunctions_4_5_Core& gl)
{
    const GLint location = gl.glGetUniformLocation(program, name);
    if (location < 0) {
        throw std::runtime_error(std::string{"Missing required uniform: "} + name);
    }
    return location;
}
