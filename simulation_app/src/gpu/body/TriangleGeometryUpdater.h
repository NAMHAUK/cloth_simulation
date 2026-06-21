#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct TriangleGeometryResources;
struct CharacterMeshTopologyResources;

class TriangleGeometryUpdater final {
public:
    TriangleGeometryUpdater() = default;
    TriangleGeometryUpdater(const TriangleGeometryUpdater&) = delete;
    TriangleGeometryUpdater& operator=(const TriangleGeometryUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const CharacterMeshTopologyResources& topology,
                const TriangleGeometryResources& triangle_geometry,
                std::uint32_t current_frame_position_begin_index,
                std::uint32_t next_frame_position_begin_index,
                float frame_alpha,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint triangle_count_location_ = -1;
    GLint current_frame_begin_location_ = -1;
    GLint next_frame_begin_location_ = -1;
    GLint frame_alpha_location_ = -1;
};
