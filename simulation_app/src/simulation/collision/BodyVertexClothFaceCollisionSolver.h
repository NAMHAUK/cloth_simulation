#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionPairBuffers.h"
#include "utils/GpuElapsedTimer.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
#define CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING 0
#endif

class BodyVertexClothFaceCollisionSolver final {
public:
    BodyVertexClothFaceCollisionSolver() = default;
    BodyVertexClothFaceCollisionSolver(const BodyVertexClothFaceCollisionSolver&) = delete;
    BodyVertexClothFaceCollisionSolver& operator=(const BodyVertexClothFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& pair_accumulate_shader_path,
                    const std::filesystem::path& pair_apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    float static_friction,
                    float dynamic_friction,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionPushoutBufferView& collision_pushout_view,
                   const ClothMeshTopologyResources& cloth_topology,
                   const CharacterVertexBufferView& character_vertex_view,
                   const CollisionPairBufferView& collision_pair_view) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionPushoutBufferView& collision_pushout_view,
               const ClothMeshTopologyResources& cloth_topology,
               const CharacterVertexBufferView& character_vertex_view,
               const CollisionPairBufferView& collision_pair_view,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct AccumulateStage final {
        GLuint program = 0;
        GLint max_pairs = -1;
        GLint thickness = -1;
    };

    struct ApplyStage final {
        GLuint program = 0;
        GLint vertex_count = -1;
        GLint max_correction = -1;
        GLint static_friction = -1;
        GLint dynamic_friction = -1;
    };

    void run_pair_accumulation_stage(const ClothMotionBufferView& motion_view, const ClothMeshTopologyResources& cloth_topology, const CharacterVertexBufferView& character_vertex_view, const CollisionPairBufferView& collision_pair_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_apply_stage(const ClothMotionBufferView& motion_view, const ClothCollisionPushoutBufferView& collision_pushout_view, const CollisionPairBufferView& collision_pair_view, QOpenGLFunctions_4_5_Core& gl) const;
    bool has_pair_programs() const;

    AccumulateStage accumulate_;
    ApplyStage apply_;
#if CLOTH_SIM_COLLISION_SOLVER_GPU_TIMING
    mutable GpuElapsedTimer accumulate_timer_;
    mutable GpuElapsedTimer apply_timer_;
#endif
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    float static_friction_ = 0.0f;
    float dynamic_friction_ = 0.0f;
};
