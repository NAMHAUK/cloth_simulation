#pragma once

#include "gpu/body/CharacterGpuStateUpdater.h"
#include "gpu/body/CharacterGpuResources.h"
#include "gpu/body/bvh/MeshBvhBoundsUpdater.h"
#include "gpu/body/bvh/VertexBvhBoundsUpdater.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/AttachmentTargetBuilder.h"
#include "gpu/scene/NormalUpdater.h"
#include "scene/SceneState.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct ShaderPaths;

class SceneGpuState final {
public:
    SceneGpuState();
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    bool is_initialized() const;
    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void update_character_frame_interpolation(const SceneState& scene,
                                              const CharacterFrameInterpolation& interpolation,
                                              QOpenGLFunctions_4_5_Core& gl);
    void update_mesh_normals(QOpenGLFunctions_4_5_Core& gl);

    const CharacterGpuResources& character_gpu_state() const;
    void set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuResources& cloth_gpu_state() const;
    void update_garment_meshes(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    bool update_garment_placement(const GarmentObject& garment,
                                  bool update_rest_lengths,
                                  QOpenGLFunctions_4_5_Core& gl);
    void build_garment_attachment_targets(SceneState& scene,
                                          std::uint32_t garment_id,
                                          QOpenGLFunctions_4_5_Core& gl);
    bool save_base_positions(QOpenGLFunctions_4_5_Core& gl);
    bool restore_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void clear_base_positions(QOpenGLFunctions_4_5_Core& gl);

private:
    CharacterGpuResources character_gpu_state_;
    MeshBvhBoundsUpdater bvh_bounds_updater_;
    VertexBvhBoundsUpdater vertex_bvh_bounds_updater_;
    NormalUpdater normal_updater_;
    CharacterGpuStateUpdater character_gpu_state_updater_;
    ClothGpuResources cloth_gpu_state_;
    AttachmentTargetBuilder attachment_target_builder_;

    bool initialized_ = false;
};
