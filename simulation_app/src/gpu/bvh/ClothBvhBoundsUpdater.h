#pragma once

#include "gpu/cloth/ClothBvhResources.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class ClothBvhBoundsUpdater final
{
public:
    ClothBvhBoundsUpdater() = default;
    ClothBvhBoundsUpdater(const ClothBvhBoundsUpdater&) = delete;
    ClothBvhBoundsUpdater& operator=(const ClothBvhBoundsUpdater&) = delete;

    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const ClothMotionBufferView& motion_view,
                const ClothBvhBufferView& bvh_view,
                const std::array<ElementRange, 2>& garment_vertex_ranges,
                float bounds_margin,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_update(const ClothMotionBufferView& motion_view,
                    const ClothBvhBufferView& bvh_view,
                    const std::array<ElementRange, 2>& garment_vertex_ranges,
                    float bounds_margin) const;

    GLuint program_ = 0;
    GLint vertex_offset_location_ = -1;
    GLint collision_triangle_offset_location_ = -1;
    GLint bvh_node_offset_location_ = -1;
    GLint level_first_node_location_ = -1;
    GLint level_node_count_location_ = -1;
    GLint bounds_margin_location_ = -1;
};
