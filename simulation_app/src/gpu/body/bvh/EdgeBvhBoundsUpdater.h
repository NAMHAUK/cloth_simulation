#pragma once

#include "gpu/body/bvh/BvhDataTypes.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct CharacterVertexBufferView;
struct EdgeBvhResources;

class EdgeBvhBoundsUpdater final {
public:
    EdgeBvhBoundsUpdater() = default;
    EdgeBvhBoundsUpdater(const EdgeBvhBoundsUpdater&) = delete;
    EdgeBvhBoundsUpdater& operator=(const EdgeBvhBoundsUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool can_update(const CharacterVertexBufferView& vertex_view,
                    const EdgeBvhResources& body_edge_bvh,
                    const std::vector<BvhNodeRange>& node_ranges_by_level,
                    float collision_thickness) const;
    void update(const CharacterVertexBufferView& vertex_view,
                const EdgeBvhResources& body_edge_bvh,
                const std::vector<BvhNodeRange>& node_ranges_by_level,
                float collision_thickness,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint first_node_location_ = -1;
    GLint node_count_location_ = -1;
    GLint collision_thickness_location_ = -1;
};
