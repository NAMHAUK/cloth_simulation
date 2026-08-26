#pragma once

#include "gpu/bvh/BvhBufferView.h"
#include "gpu/character/CharacterGpuDataTypes.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class BodyBvhBoundsUpdater final
{
public:
    BodyBvhBoundsUpdater() = default;
    BodyBvhBoundsUpdater(const BodyBvhBoundsUpdater&) = delete;
    BodyBvhBoundsUpdater& operator=(const BodyBvhBoundsUpdater&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    float detection_distance,
                    QOpenGLFunctions_4_5_Core& gl);
    void set_level_offsets(const std::vector<std::uint32_t>& triangle_level_offsets,
                           const std::vector<std::uint32_t>& vertex_level_offsets,
                           const std::vector<std::uint32_t>& edge_level_offsets);
    void update(const CharacterMeshTopologyResources& topology,
                const CharacterVertexBufferView& vertex_view,
                const BodyTriangleResources& body_triangles,
                const BvhBufferView& body_triangle_bvh,
                const BvhBufferView& body_vertex_bvh,
                const BvhBufferView& body_edge_bvh,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint triangle_first_node_index_location_ = -1;
    GLint triangle_node_count_location_ = -1;
    GLint vertex_first_node_index_location_ = -1;
    GLint vertex_node_count_location_ = -1;
    GLint edge_first_node_index_location_ = -1;
    GLint edge_node_count_location_ = -1;
    GLint detection_distance_location_ = -1;

    std::vector<std::uint32_t> triangle_level_offsets_;
    std::vector<std::uint32_t> vertex_level_offsets_;
    std::vector<std::uint32_t> edge_level_offsets_;
};
