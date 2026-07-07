#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "utils/GpuElapsedTimer.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

#ifndef CLOTH_SIM_COLLISION_GPU_TIMING
#define CLOTH_SIM_COLLISION_GPU_TIMING 1
#endif

class ClothVertexBodyFaceCollisionSolver final {
public:
    ClothVertexBodyFaceCollisionSolver() = default;
    ClothVertexBodyFaceCollisionSolver(const ClothVertexBodyFaceCollisionSolver&) = delete;
    ClothVertexBodyFaceCollisionSolver& operator=(const ClothVertexBodyFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    std::uint32_t ignored_body_part_mask,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const TriangleGeometryResources& character_geometry,
                   const TriangleBvhResources& character_bvh) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const TriangleGeometryResources& character_geometry,
               const TriangleBvhResources& character_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint cloth_vertex_count_location_ = -1;
    GLint collision_thickness_location_ = -1;
    GLint max_correction_length_location_ = -1;
    GLint ignored_body_part_mask_location_ = -1;
#if CLOTH_SIM_COLLISION_GPU_TIMING
    mutable GpuElapsedTimer solve_timer_;
#endif
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
    std::uint32_t ignored_body_part_mask_ = 0;
};
