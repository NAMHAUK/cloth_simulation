#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct VertexTriangleAdjacency final {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> triangles;
    std::uint32_t triangle_count = 0;

    bool is_valid(std::uint32_t vertex_count) const;
};

struct NormalUpdateInputs final {
    GLuint position_buffer = 0;
    GLuint index_buffer = 0;
    GLuint adjacency_offset_buffer = 0;
    GLuint adjacency_triangle_buffer = 0;

    GLuint triangle_normal_buffer = 0;
    GLuint vertex_normal_buffer = 0;

    std::uint32_t position_component_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
};

bool build_vertex_triangle_adjacency(std::uint32_t vertex_count,
                                     const std::vector<std::uint32_t>& triangle_indices,
                                     VertexTriangleAdjacency& adjacency);

class NormalUpdater final {
public:
    NormalUpdater() = default;
    NormalUpdater(const NormalUpdater&) = delete;
    NormalUpdater& operator=(const NormalUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void update_normals(const NormalUpdateInputs& inputs, QOpenGLFunctions_4_5_Core& gl) const;

private:
    // Shader loading
    GLuint load_compute_program(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl) const;
    GLuint compile_compute_shader(const char* source, QOpenGLFunctions_4_5_Core& gl) const;

    // Program objects
    GLuint triangle_program_ = 0;
    GLuint vertex_program_ = 0;

    // Uniform locations
    GLint triangle_count_location_ = -1;
    GLint position_component_offset_location_ = -1;
    GLint vertex_count_location_ = -1;
};
