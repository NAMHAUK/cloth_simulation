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

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void solve(const ClothGpuState& cloth_state, GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint vertex_offset_loc_ = -1;
    GLint vertex_count_loc_ = -1;
    float search_radius_ = 0.0f;
    float pushout_margin_ = 0.0f;
};
