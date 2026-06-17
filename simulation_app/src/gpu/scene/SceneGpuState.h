#pragma once

#include "gpu/body/CharacterGpuResources.h"
#include "gpu/collision/MeshBvhBoundsUpdater.h"
#include "gpu/body/TriangleGeometryUpdater.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/NormalUpdater.h"
#include "scene/SceneState.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

struct ShaderPaths;

class SceneGpuState final {
public:
    SceneGpuState() = default;
    SceneGpuState(const SceneGpuState&) = delete;
    SceneGpuState& operator=(const SceneGpuState&) = delete;

    bool is_initialized() const;
    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);
    void update_character_frame(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void update_character_frame_interpolation(const SceneState& scene,
                                              const CharacterFrameInterpolation& interpolation,
                                              QOpenGLFunctions_4_5_Core& gl);
    void update_character_render_frame(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
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

private:
    void update_character_triangle_geometry(const CharacterFrameInterpolation& interpolation,
                                            QOpenGLFunctions_4_5_Core& gl);
    void update_character_bvh_bounds(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);

    CharacterGpuResources character_gpu_state_;
    ClothGpuResources cloth_gpu_state_;
    TriangleGeometryUpdater triangle_geometry_updater_;
    MeshBvhBoundsUpdater bvh_bounds_updater_;
    NormalUpdater normal_updater_;

    bool initialized_ = false;
};
