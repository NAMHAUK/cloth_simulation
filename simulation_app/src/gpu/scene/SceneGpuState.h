#pragma once

#include "gpu/bvh/BvhBoundsUpdater.h"
#include "gpu/character/CharacterGpuState.h"
#include "gpu/cloth/ClothGpuState.h"
#include "gpu/scene/SimulationBufferBindings.h"
#include "gpu/scene/SimulationGpuView.h"
#include "simulation/SceneState.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState final
{
public:
    SceneGpuState() = default;
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    float attachment_surface_offset,
                    float body_detection_distance,
                    const SceneState& scene,
                    QOpenGLFunctions_4_5_Core& gl);
    void set_character_motion(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void update_character_pose(const SceneState& scene, float frame_alpha, QOpenGLFunctions_4_5_Core& gl);
    void rebuild_garment_resources(const SceneState& scene,
                                   QOpenGLFunctions_4_5_Core& gl,
                                   GarmentLayer changed_layer);
    void rebuild_collision_buffers(QOpenGLFunctions_4_5_Core& gl);
    void upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void initialize_garment_attachments(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void update_cloth_bvh_bounds(const SimulationGpuView& views,
                                 float bounds_margin,
                                 QOpenGLFunctions_4_5_Core& gl);
    void update_cloth_normals(QOpenGLFunctions_4_5_Core& gl);
    bool is_initialized() const;
    SimulationGpuView simulation_view() const;
    const CharacterGpuState& character_gpu_state() const;
    const ClothGpuState& cloth_gpu_state() const;
    void release(QOpenGLFunctions_4_5_Core& gl);
    void release_garment_resources(QOpenGLFunctions_4_5_Core& gl);

private:
    void initialize_normal_programs(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void initialize_attachment_target_program(const std::filesystem::path& shader_dir,
                                              float surface_offset,
                                              QOpenGLFunctions_4_5_Core& gl);
    void initialize_character_resources(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void build_attachment_targets(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl);
    void release_collision_buffers(QOpenGLFunctions_4_5_Core& gl);

    void update_character_vertex_normals(QOpenGLFunctions_4_5_Core& gl);

    BvhBoundsUpdater bvh_bounds_updater_;
    SimulationBufferBindings buffer_bindings_;
    CharacterGpuState character_gpu_state_;
    ClothGpuState cloth_gpu_state_;
    CollisionBuffers collision_buffers_;

    GLuint triangle_normal_program_ = 0;
    GLuint vertex_normal_program_ = 0;
    GLint triangle_count_location_ = -1;
    GLint vertex_count_location_ = -1;
    GLint use_character_buffers_location_ = -1;

    GLuint attachment_target_program_ = 0;
    GLint attachment_constraint_offset_location_ = -1;
    GLint attachment_constraint_count_location_ = -1;

    bool initialized_ = false;
};
