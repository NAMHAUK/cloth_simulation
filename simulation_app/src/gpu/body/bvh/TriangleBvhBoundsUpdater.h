#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/body/bvh/BvhDataTypes.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class TriangleBvhBoundsUpdater final {
public:
    TriangleBvhBoundsUpdater() = default;
    TriangleBvhBoundsUpdater(const TriangleBvhBoundsUpdater&) = delete;
    TriangleBvhBoundsUpdater& operator=(const TriangleBvhBoundsUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const CharacterMeshTopologyResources& topology,
                const CharacterVertexBufferView& vertex_view,
                const TriangleGeometryResources& character_geometry,
                const TriangleBvhResources& character_bvh,
                const std::vector<BvhNodeRange>& node_ranges_by_level,
                float collision_thickness,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_update(const CharacterMeshTopologyResources& topology,
                    const CharacterVertexBufferView& vertex_view,
                    const TriangleGeometryResources& character_geometry,
                    const TriangleBvhResources& character_bvh,
                    const std::vector<BvhNodeRange>& node_ranges_by_level,
                    float collision_thickness) const;

    GLuint program_ = 0;
    GLint first_node_location_ = -1;
    GLint node_count_location_ = -1;
    GLint collision_thickness_location_ = -1;
};
