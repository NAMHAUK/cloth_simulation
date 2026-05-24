#pragma once

#include "gpu/CharacterGpuState.h"
#include "gpu/ClothGpuState.h"
#include "simulation/SimulationScene.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class SimulationGpuState final {
public:
    SimulationGpuState() = default;
    SimulationGpuState(const SimulationGpuState&) = delete;
    SimulationGpuState& operator=(const SimulationGpuState&) = delete;

    bool is_initialized() const;
    bool initialize(QOpenGLFunctions_4_5_Core& gl);
    void sync(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    const CharacterGpuState& character_gpu_state() const;
    void set_character_mesh(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl);

    const ClothGpuState* garment_gpu_state(GarmentId garment_id) const;
    void set_garment_mesh(const GarmentSceneObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void add_garment_gpu_state(GarmentId garment_id);
    void remove_garment_gpu_state(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl);

private:
    struct GarmentGpuSlot {
        GarmentId id = 0;
        ClothGpuState gpu_state;
        std::uint64_t uploaded_revision = 0;
    };

    void update_character_frame(const SimulationScene& scene);
    GarmentGpuSlot* find_garment_gpu_slot(GarmentId garment_id);
    const GarmentGpuSlot* find_garment_gpu_slot(GarmentId garment_id) const;

    CharacterGpuState character_gpu_state_;
    std::vector<GarmentGpuSlot> garment_gpu_slots_;

    std::uint64_t character_revision_ = 0;
    std::uint32_t current_character_frame_ = 0;
    bool initialized_ = false;
    bool character_uploaded_ = false;
};
