#pragma once

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

struct ElementRange;
struct SimulationGpuView;

class AttachmentTargetBuilder final
{
public:
    AttachmentTargetBuilder() = default;
    AttachmentTargetBuilder(const AttachmentTargetBuilder&) = delete;
    AttachmentTargetBuilder& operator=(const AttachmentTargetBuilder&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    bool build(const SimulationGpuView& views,
               const ElementRange& target_range,
               float surface_offset,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool can_build(const SimulationGpuView& views, const ElementRange& target_range) const;

    GLuint program_ = 0;
    GLint constraint_offset_location_ = -1;
    GLint constraint_count_location_ = -1;
    GLint surface_offset_location_ = -1;
};
