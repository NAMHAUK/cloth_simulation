#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class GarmentPrefitSolver final {
public:
    GarmentPrefitSolver() = default;
    GarmentPrefitSolver(const GarmentPrefitSolver&) = delete;
    GarmentPrefitSolver& operator=(const GarmentPrefitSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path,
                    float search_radius,
                    float pushout_margin,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const GarmentBufferRanges& garment_range,
                   const TriangleGeometryResources& character_geometry,
                   const TriangleBvhResources& character_bvh) const;
    void solve(const ClothMotionBufferView& motion_view,
               const GarmentBufferRanges& garment_range,
               const TriangleGeometryResources& character_geometry,
               const TriangleBvhResources& character_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_offset_location_ = -1;
    GLint vertex_count_location_ = -1;
    GLint search_radius_squared_location_ = -1;
    GLint pushout_margin_location_ = -1;
    float search_radius_ = 0.0f;
    float pushout_margin_ = 0.0f;
};
