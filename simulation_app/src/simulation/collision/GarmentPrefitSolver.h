#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ElementRange;
struct PrefitParams;
struct SimulationGpuView;

class GarmentPrefitSolver final
{
public:
    explicit GarmentPrefitSolver(const PrefitParams& params);
    GarmentPrefitSolver(const GarmentPrefitSolver&) = delete;
    GarmentPrefitSolver& operator=(const GarmentPrefitSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuView& views, const ElementRange& vertex_range) const;
    void solve(const SimulationGpuView& views,
               const ElementRange& vertex_range,
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
