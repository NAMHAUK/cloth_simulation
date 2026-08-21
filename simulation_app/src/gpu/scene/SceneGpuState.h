#pragma once

#include "gpu/bvh/BodyBvhBoundsUpdater.h"
#include "gpu/character/CharacterGpuResources.h"
#include "gpu/character/CharacterGpuStateUpdater.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/AttachmentTargetBuilder.h"
#include "gpu/scene/CollisionCandidateBuffers.h"
#include "gpu/scene/NormalUpdater.h"
#include "gpu/scene/SimulationGpuView.h"
#include "scene/SceneState.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState final
{
public:
    SceneGpuState();
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir,
                    float attachment_surface_offset,
                    QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void update_character_pose(const SceneState& scene,
                               float frame_alpha,
                               float body_detection_distance,
                               QOpenGLFunctions_4_5_Core& gl);
    void update_cloth_normals(QOpenGLFunctions_4_5_Core& gl);
    SimulationGpuView simulation_view() const;

    const CharacterGpuResources& character_gpu_state() const;
    void set_character_motion(const SceneState& scene,
                              float body_detection_distance,
                              QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuResources& cloth_gpu_state() const;
    void release_garment_resources(QOpenGLFunctions_4_5_Core& gl);
    void rebuild_garment_resources(const SceneState& scene,
                                   QOpenGLFunctions_4_5_Core& gl,
                                   GarmentLayer changed_layer);
    void upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void build_garment_attachment_targets(SceneState& scene,
                                          GarmentLayer layer,
                                          QOpenGLFunctions_4_5_Core& gl);
    void capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);

private:
    CharacterGpuResources character_gpu_state_;
    BodyBvhBoundsUpdater bvh_bounds_updater_;
    NormalUpdater normal_updater_;
    CharacterGpuStateUpdater character_gpu_state_updater_;
    ClothGpuResources cloth_gpu_state_;
    CollisionCandidateBuffers collision_candidate_buffers_;
    AttachmentTargetBuilder attachment_target_builder_;

    bool initialized_ = false;
};
