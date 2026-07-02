#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class BodyVertexClothFaceCollisionSolver final {
public:
    BodyVertexClothFaceCollisionSolver() = default;
    BodyVertexClothFaceCollisionSolver(const BodyVertexClothFaceCollisionSolver&) = delete;
    BodyVertexClothFaceCollisionSolver& operator=(const BodyVertexClothFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const ClothMeshTopologyResources& cloth_topology,
                   const ClothTriangleColorView& triangle_color_view,
                   const CharacterVertexBufferView& character_vertex_view,
                   const CharacterMeshTopologyResources& character_topology,
                   const MeshBvhResources& character_bvh) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const ClothMeshTopologyResources& cloth_topology,
               const ClothTriangleColorView& triangle_color_view,
               const CharacterVertexBufferView& character_vertex_view,
               const CharacterMeshTopologyResources& character_topology,
               const MeshBvhResources& character_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint triangle_color_offset_location_ = -1;
    GLint triangle_dispatch_count_location_ = -1;
    GLint root_node_index_location_ = -1;
    GLint max_contacts_per_vertex_location_ = -1;
    GLint collision_thickness_location_ = -1;
    GLint max_correction_length_location_ = -1;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
};
