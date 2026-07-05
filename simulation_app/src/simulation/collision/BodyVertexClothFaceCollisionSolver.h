#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionWorkspaceBuffers.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class BodyVertexClothFaceCollisionSolver final {
public:
    BodyVertexClothFaceCollisionSolver() = default;
    BodyVertexClothFaceCollisionSolver(const BodyVertexClothFaceCollisionSolver&) = delete;
    BodyVertexClothFaceCollisionSolver& operator=(const BodyVertexClothFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& pair_generate_shader_path,
                    const std::filesystem::path& pair_accumulate_shader_path,
                    const std::filesystem::path& pair_apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    std::uint32_t ignored_body_part_mask,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const ClothMeshTopologyResources& cloth_topology,
                   const CharacterVertexBufferView& character_vertex_view,
                   const VertexBvhResources& body_vertex_bvh,
                   const CollisionWorkspaceBufferView& collision_workspace_view) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const ClothMeshTopologyResources& cloth_topology,
               const CharacterVertexBufferView& character_vertex_view,
               const VertexBvhResources& body_vertex_bvh,
               const CollisionWorkspaceBufferView& collision_workspace_view,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct GenerateStage final {
        GLuint program = 0;
        GLint triangle_count = -1;
        GLint max_pairs = -1;
        GLint thickness = -1;
        GLint ignored_body_part_mask = -1;
    };

    struct AccumulateStage final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint thickness = -1;
    };

    struct ApplyStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
    };

    void run_pair_generation_stage(const ClothMotionBufferView& motion_view, const ClothMeshTopologyResources& cloth_topology, const CharacterVertexBufferView& character_vertex_view, const VertexBvhResources& body_vertex_bvh, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_accumulation_stage(const ClothMotionBufferView& motion_view, const ClothMeshTopologyResources& cloth_topology, const CharacterVertexBufferView& character_vertex_view, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_apply_stage(const ClothMotionBufferView& motion_view, const ClothCollisionStateBufferView& collision_view, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    bool has_pair_programs() const;

    GenerateStage generate_;
    AccumulateStage accumulate_;
    ApplyStage apply_;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    std::uint32_t ignored_body_part_mask_ = 0;
};
