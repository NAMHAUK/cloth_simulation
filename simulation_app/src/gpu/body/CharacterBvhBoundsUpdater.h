#pragma once

#include "gpu/body/CharacterGpuResources.h"

#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class CharacterBvhBoundsUpdater final {
public:
    CharacterBvhBoundsUpdater() = default;
    CharacterBvhBoundsUpdater(const CharacterBvhBoundsUpdater&) = delete;
    CharacterBvhBoundsUpdater& operator=(const CharacterBvhBoundsUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    void update(const CharacterTriangleGeometryResources& character_geometry,
                const CharacterBvhResources& character_bvh,
                const std::vector<BvhBoundsUpdateLevelRange>& level_ranges,
                QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint program_ = 0;
    GLint first_node_location_ = -1;
    GLint node_count_location_ = -1;
};
