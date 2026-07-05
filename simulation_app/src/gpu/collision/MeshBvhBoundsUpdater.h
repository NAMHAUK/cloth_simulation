#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/collision/BvhDataTypes.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class MeshBvhBoundsUpdater final {
public:
    MeshBvhBoundsUpdater() = default;
    MeshBvhBoundsUpdater(const MeshBvhBoundsUpdater&) = delete;
    MeshBvhBoundsUpdater& operator=(const MeshBvhBoundsUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const TriangleGeometryResources& character_geometry,
                const MeshBvhResources& character_bvh,
                const std::vector<BvhNodeRange>& node_ranges_by_level,
                float collision_thickness,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_update(const TriangleGeometryResources& character_geometry,
                    const MeshBvhResources& character_bvh,
                    const std::vector<BvhNodeRange>& node_ranges_by_level,
                    float collision_thickness) const;

    GLuint program_ = 0;
    GLint first_node_location_ = -1;
    GLint node_count_location_ = -1;
    GLint collision_thickness_location_ = -1;
};
