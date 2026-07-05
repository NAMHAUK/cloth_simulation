#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class AttachmentTargetBuilder final {
public:
    AttachmentTargetBuilder() = default;
    AttachmentTargetBuilder(const AttachmentTargetBuilder&) = delete;
    AttachmentTargetBuilder& operator=(const AttachmentTargetBuilder&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& shader_path, QOpenGLFunctions_4_5_Core& gl);
    bool build(const ClothMotionBufferView& motion_view,
               const AttachmentConstraintBufferView& attachment_view,
               const ConstraintRange& target_range,
               const TriangleGeometryResources& character_geometry,
               const MeshBvhResources& character_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_build(const ClothMotionBufferView& motion_view,
                   const AttachmentConstraintBufferView& attachment_view,
                   const ConstraintRange& target_range,
                   const TriangleGeometryResources& character_geometry,
                   const MeshBvhResources& character_bvh) const;

    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint root_node_index_location_ = -1;
    GLint surface_offset_location_ = -1;
};
