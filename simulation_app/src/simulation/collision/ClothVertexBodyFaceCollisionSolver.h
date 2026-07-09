#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionPairBuffers.h"
#include "utils/GpuElapsedTimer.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_GPU_TIMING
#define CLOTH_SIM_COLLISION_GPU_TIMING 0
#endif

class ClothVertexBodyFaceCollisionSolver final {
public:
    ClothVertexBodyFaceCollisionSolver() = default;
    ClothVertexBodyFaceCollisionSolver(const ClothVertexBodyFaceCollisionSolver&) = delete;
    ClothVertexBodyFaceCollisionSolver& operator=(const ClothVertexBodyFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& pair_accumulate_shader_path,
                    const std::filesystem::path& pair_apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const TriangleGeometryResources& character_geometry,
                   const CollisionPairBufferView& collision_pair_view) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const TriangleGeometryResources& character_geometry,
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
    };

    void run_pair_accumulation_stage(const ClothMotionBufferView& motion_view, const TriangleGeometryResources& character_geometry, const CollisionPairBufferView& collision_pair_view, QOpenGLFunctions_4_5_Core& gl) const;
    void run_pair_apply_stage(const ClothMotionBufferView& motion_view, const ClothCollisionStateBufferView& collision_view, const CollisionPairBufferView& collision_pair_view, QOpenGLFunctions_4_5_Core& gl) const;
    bool has_pair_programs() const;

    AccumulateStage accumulate_;
    ApplyStage apply_;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    mutable GpuElapsedTimer accumulate_timer_;
    mutable GpuElapsedTimer apply_timer_;
#endif
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
};
