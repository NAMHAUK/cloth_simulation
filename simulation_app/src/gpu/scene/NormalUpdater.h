#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterNormalResources;
struct CharacterMeshTopologyResources;
struct ClothMeshTopologyResources;
struct ClothNormalResources;

class NormalUpdater final
{
public:
    NormalUpdater() = default;
    NormalUpdater(const NormalUpdater&) = delete;
    NormalUpdater& operator=(const NormalUpdater&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void update_cloth_normals(const ClothMeshTopologyResources& topology,
                              const ClothNormalResources& normals,
                              QOpenGLFunctions_4_5_Core& gl) const;
    void update_character_normals(const CharacterMeshTopologyResources& topology,
                                  const CharacterNormalResources& normals,
                                  QOpenGLFunctions_4_5_Core& gl) const;

private:
    void update_vertex_normals(GLuint triangle_normal_source_buffer,
                               GLuint adjacent_triangle_offsets_buffer,
                               GLuint adjacent_triangle_indices_buffer,
                               GLuint vertex_normal_buffer,
                               std::uint32_t vertex_count,
                               std::uint32_t triangle_normal_stride,
                               std::uint32_t triangle_normal_offset,
                               QOpenGLFunctions_4_5_Core& gl) const;

    // Program objects
    GLuint triangle_program_ = 0;
    GLuint vertex_program_ = 0;

    // Uniform locations
    GLint triangle_count_location_ = -1;
    GLint position_component_offset_location_ = -1;
    GLint vertex_count_location_ = -1;
    GLint triangle_normal_stride_location_ = -1;
    GLint triangle_normal_offset_location_ = -1;
};
