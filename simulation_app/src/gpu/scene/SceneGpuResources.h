#pragma once

#include "gpu/body/CharacterGpuResources.h"
#include "gpu/cloth/ClothGpuResources.h"
#include "gpu/scene/NormalUpdater.h"
#include "scene/SceneState.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuResources final {
public:
    SceneGpuResources() = default;
    SceneGpuResources(const SceneGpuResources&) = delete;
    SceneGpuResources& operator=(const SceneGpuResources&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    void sync(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    const CharacterGpuResources& character_gpu_state() const;
    void set_character_mesh(const SceneState& scene, QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuResources* garment_gpu_state(GarmentId garment_id) const;
    void set_garment_mesh(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void add_garment_gpu_state(GarmentId garment_id);
    void remove_garment_gpu_state(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl);

private:
    struct GarmentGpuSlot {
        GarmentId id = 0;
        ClothGpuResources gpu_state;
        std::uint64_t uploaded_revision = 0;
    };

    void update_character_frame(const SceneState& scene);
    GarmentGpuSlot* find_garment_gpu_slot(GarmentId garment_id);
    const GarmentGpuSlot* find_garment_gpu_slot(GarmentId garment_id) const;

    CharacterGpuResources character_gpu_state_;
    NormalUpdater normal_updater_;
    std::vector<GarmentGpuSlot> garment_gpu_slots_;

    std::uint64_t character_revision_ = 0;
    std::uint32_t current_character_frame_ = 0;
    bool initialized_ = false;
    bool character_uploaded_ = false;
};
