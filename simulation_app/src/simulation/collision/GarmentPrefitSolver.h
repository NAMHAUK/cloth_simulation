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
    bool can_solve(const ClothPositionBufferView& position_view,
                   const TriangleGeometryResources& character_geometry,
                   const MeshBvhResources& character_bvh) const;
    void solve(const ClothPositionBufferView& position_view,
               const TriangleGeometryResources& character_geometry,
               const MeshBvhResources& character_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint cloth_vertex_count_location_ = -1;
    GLint root_node_index_location_ = -1;
    GLint search_radius_location_ = -1;
    GLint pushout_margin_location_ = -1;
    float search_radius_ = 0.0f;
    float pushout_margin_ = 0.0f;
};
