#pragma once

#include "gpu/character/CharacterGpuState.h"
#include "gpu/cloth/ClothGpuState.h"
#include "gpu/scene/PbdGpuState.h"
#include "gpu/scene/SimulationBufferBindings.h"
#include "simulation/SceneState.h"

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
    void rebuild_garment_resources(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl, GarmentLayer changed_layer);
    void create_simulation_buffers(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void initialize_garment_attachments(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void update_cloth_bvh_bounds(float bounds_margin, QOpenGLFunctions_4_5_Core& gl);
    void update_cloth_normals(QOpenGLFunctions_4_5_Core& gl);
    bool is_initialized() const;
    const CharacterGpuState& character_gpu_state() const;
    const ClothGpuState& cloth_gpu_state() const;
    PbdGpuState& pbd_gpu_state();
    const PbdGpuState& pbd_gpu_state() const;
    const CollisionBuffers& collision_buffers() const;
    void release(QOpenGLFunctions_4_5_Core& gl);
    void release_garment_resources(QOpenGLFunctions_4_5_Core& gl);

private:
    // Collision candidate buffers
    void release_collision_buffers(QOpenGLFunctions_4_5_Core& gl);

    void update_character_vertex_normals(QOpenGLFunctions_4_5_Core& gl);

    SimulationBufferBindings buffer_bindings_;
    CharacterGpuState character_gpu_state_;
    ClothGpuState cloth_gpu_state_;
    PbdGpuState pbd_gpu_state_;

    bool initialized_ = false;
};
