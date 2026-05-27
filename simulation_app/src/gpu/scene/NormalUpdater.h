#pragma once

#include "gpu/scene/MeshBufferResources.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class NormalUpdater final {
public:
    NormalUpdater() = default;
    NormalUpdater(const NormalUpdater&) = delete;
    NormalUpdater& operator=(const NormalUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void update_normals(const MeshTopologyResources& topology,
                        const MeshNormalResources& normals,
                        QOpenGLFunctions_4_5_Core& gl) const;

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
