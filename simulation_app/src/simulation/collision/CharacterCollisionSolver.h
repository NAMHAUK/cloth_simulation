#pragma once

#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/MeshBufferResources.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class CharacterCollisionSolver final {
public:
    CharacterCollisionSolver() = default;
    CharacterCollisionSolver(const CharacterCollisionSolver&) = delete;
    CharacterCollisionSolver& operator=(const CharacterCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path,float collision_thickness, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothPositionBufferView& position_view, const MeshTopologyResources& character_topology) const;
    void solve(const ClothPositionBufferView& position_view,
               const MeshTopologyResources& character_topology,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint cloth_vertex_count_location_ = -1;
    GLint character_triangle_count_location_ = -1;
    GLint character_position_component_offset_location_ = -1;
    GLint collision_thickness_location_ = -1;
    float collision_thickness_ = 0.0f;
};
