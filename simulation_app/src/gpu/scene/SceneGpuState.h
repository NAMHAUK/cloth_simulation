#pragma once

#include "gpu/bvh/BodyBvhBoundsUpdater.h"
#include "gpu/character/CharacterGpuResources.h"
#include "gpu/character/CharacterGpuStateUpdater.h"
#include "gpu/cloth/ClothBvhResources.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/AttachmentTargetBuilder.h"
#include "gpu/scene/CollisionCandidateBuffers.h"
#include "gpu/scene/NormalUpdater.h"
#include "gpu/scene/SimulationGpuView.h"
#include "scene/SceneState.h"

#include <filesystem>
#include <optional>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState final
{
public:
    SceneGpuState();
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void update_character_pose(const SceneState& scene,
                               float frame_alpha,
                               float body_collision_thickness,
                               QOpenGLFunctions_4_5_Core& gl);
    void update_mesh_normals(QOpenGLFunctions_4_5_Core& gl);
    SimulationGpuView simulation_view() const;

    const CharacterGpuResources& character_gpu_state() const;
    void set_character_mesh(const SceneState& scene,
                            float body_collision_thickness,
                            QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuResources& cloth_gpu_state() const;
    ClothBvhBufferView cloth_bvh_buffer_view() const;
    CollisionCandidateBufferView collision_candidate_buffer_view() const;
    void update_garment_meshes(const SceneState& scene,
                               QOpenGLFunctions_4_5_Core& gl,
                               std::optional<GarmentLayer> updated_layer = std::nullopt);
    void update_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    bool build_garment_attachment_targets(SceneState& scene,
                                          GarmentLayer layer,
                                          float surface_offset,
                                          QOpenGLFunctions_4_5_Core& gl);
    bool save_base_positions(QOpenGLFunctions_4_5_Core& gl);
    bool restore_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void clear_base_positions(QOpenGLFunctions_4_5_Core& gl);

private:
    CharacterGpuResources character_gpu_state_;
    BodyBvhBoundsUpdater bvh_bounds_updater_;
    NormalUpdater normal_updater_;
    CharacterGpuStateUpdater character_gpu_state_updater_;
    ClothGpuResources cloth_gpu_state_;
    ClothBvhResources cloth_bvh_resources_;
    CollisionCandidateBuffers collision_candidate_buffers_;
    AttachmentTargetBuilder attachment_target_builder_;

    bool initialized_ = false;
};
