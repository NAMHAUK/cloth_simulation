#pragma once

#include "gpu/bvh/BvhBoundsUpdater.h"
#include "gpu/scene/SimulationBufferBindings.h"

#include <cstdint>
#include <filesystem>

class ClothGpuState;
class SceneState;
struct ClothBufferElementCounts;
struct GarmentBufferState;

class PbdGpuState final
{
public:
    PbdGpuState() = default;
    PbdGpuState(const PbdGpuState&) = delete;
    PbdGpuState& operator=(const PbdGpuState&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    float attachment_surface_offset,
                    float body_detection_distance,
                    const SceneState& scene,
                    QOpenGLFunctions_4_5_Core& gl);
    void create_collision_buffers(const ClothBufferElementCounts& counts, QOpenGLFunctions_4_5_Core& gl);
    void release_collision_buffers(QOpenGLFunctions_4_5_Core& gl);
    void update_body_bvh_bounds(QOpenGLFunctions_4_5_Core& gl) const;
    void update_cloth_bvh_bounds(const ClothGpuState& cloth_state,
                                 float bounds_margin,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    void build_attachment_targets(const GarmentBufferState& garment_state, QOpenGLFunctions_4_5_Core& gl) const;
    void update_cloth_normals(const ClothBufferElementCounts& counts, QOpenGLFunctions_4_5_Core& gl) const;
    void update_character_vertex_normals(std::uint32_t vertex_count, QOpenGLFunctions_4_5_Core& gl) const;
    const CollisionBuffers& collision_buffers() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    BvhBoundsUpdater bvh_bounds_updater_;
    GLuint triangle_normal_program_ = 0;
    GLuint vertex_normal_program_ = 0;
    GLint triangle_count_loc_ = -1;
    GLint vertex_count_loc_ = -1;
    GLint use_character_buffers_loc_ = -1;

    GLuint attachment_target_program_ = 0;
    GLint attachment_constraint_offset_loc_ = -1;
    GLint attachment_constraint_count_loc_ = -1;

    CollisionBuffers collision_buffers_;
};
