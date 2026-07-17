#pragma once

#include "gpu/character/CharacterGpuDataTypes.h"
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
               const ElementRange& target_range,
               const TriangleGeometryResources& body_triangle_geometry,
               const TriangleBvhResources& body_triangle_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_build(const ClothMotionBufferView& motion_view,
                   const AttachmentConstraintBufferView& attachment_view,
                   const ElementRange& target_range,
                   const TriangleGeometryResources& body_triangle_geometry,
                   const TriangleBvhResources& body_triangle_bvh) const;

    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint surface_offset_location_ = -1;
};
