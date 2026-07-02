#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionWorkspaceBuffers.h"
#include "utils/GpuElapsedTimer.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

// GPU timing stays opt-in at compile time because GL_TIME_ELAPSED has runtime cost.
#ifndef CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
#define CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING 1
#endif

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
    };

    struct AccumulateStage final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint thickness = -1;
        GLint contact_capacity = -1;
    };

    struct ApplyStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_contacts = -1;
        GLint contact_capacity = -1;
        GLint max_correction = -1;
    };

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    mutable GpuElapsedTimer gpu_timer_;
#endif

    void run_pair_generation_stage(const ClothMotionBufferView& motion_view, const ClothMeshTopologyResources& cloth_topology, const CharacterVertexBufferView& character_vertex_view, const VertexBvhResources& body_vertex_bvh, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_accumulation_stage(const ClothMotionBufferView& motion_view, const ClothMeshTopologyResources& cloth_topology, const CharacterVertexBufferView& character_vertex_view, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_apply_stage(const ClothMotionBufferView& motion_view, const ClothCollisionStateBufferView& collision_view, const CollisionWorkspaceBufferView& collision_workspace_view, QOpenGLFunctions_4_5_Core& gl) const;
    bool has_pair_programs() const;

    GenerateStage generate_;
    AccumulateStage accumulate_;
    ApplyStage apply_;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
};
