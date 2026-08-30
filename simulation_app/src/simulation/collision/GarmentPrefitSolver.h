#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct PrefitParams;
class ClothGpuState;

class GarmentPrefitSolver final
{
public:
    explicit GarmentPrefitSolver(const PrefitParams& params);
    GarmentPrefitSolver(const GarmentPrefitSolver&) = delete;
    GarmentPrefitSolver& operator=(const GarmentPrefitSolver&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothGpuState& cloth_state, GarmentLayer layer) const;
    void solve(const ClothGpuState& cloth_state, GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const;
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
