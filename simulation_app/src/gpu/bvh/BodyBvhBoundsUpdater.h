#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
#include "gpu/bvh/BvhDataTypes.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class BodyBvhBoundsUpdater final {
public:
    BodyBvhBoundsUpdater() = default;
    BodyBvhBoundsUpdater(const BodyBvhBoundsUpdater&) = delete;
    BodyBvhBoundsUpdater& operator=(const BodyBvhBoundsUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const CharacterMeshTopologyResources& topology,
                const CharacterVertexBufferView& vertex_view,
                const TriangleGeometryResources& body_triangle_geometry,
                const TriangleBvhResources& body_triangle_bvh,
                const VertexBvhResources& body_vertex_bvh,
                const EdgeBvhResources& body_edge_bvh,
                const std::vector<BvhNodeRange>& triangle_node_ranges_by_level,
                const std::vector<BvhNodeRange>& vertex_node_ranges_by_level,
                const std::vector<BvhNodeRange>& edge_node_ranges_by_level,
                float collision_thickness,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_update(const CharacterMeshTopologyResources& topology,
                    const CharacterVertexBufferView& vertex_view,
                    const TriangleGeometryResources& body_triangle_geometry,
                    const TriangleBvhResources& body_triangle_bvh,
                    const VertexBvhResources& body_vertex_bvh,
                    const EdgeBvhResources& body_edge_bvh,
                    const std::vector<BvhNodeRange>& triangle_node_ranges_by_level,
                    const std::vector<BvhNodeRange>& vertex_node_ranges_by_level,
                    const std::vector<BvhNodeRange>& edge_node_ranges_by_level,
                    float collision_thickness) const;

    GLuint program_ = 0;
    GLint triangle_first_node_location_ = -1;
    GLint triangle_node_count_location_ = -1;
    GLint vertex_first_node_location_ = -1;
    GLint vertex_node_count_location_ = -1;
    GLint edge_first_node_location_ = -1;
    GLint edge_node_count_location_ = -1;
    GLint collision_thickness_location_ = -1;
};
