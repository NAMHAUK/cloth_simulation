#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct SimulationGpuView;

class ClothBvhBoundsUpdater final
{
public:
    ClothBvhBoundsUpdater() = default;
    ClothBvhBoundsUpdater(const ClothBvhBoundsUpdater&) = delete;
    ClothBvhBoundsUpdater& operator=(const ClothBvhBoundsUpdater&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void update(const SimulationGpuView& views, float bounds_margin, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_update(const SimulationGpuView& views, float bounds_margin) const;

    GLuint program_ = 0;
    GLint level_first_node_location_ = -1;
    GLint level_node_count_location_ = -1;
    GLint bounds_margin_location_ = -1;
};
