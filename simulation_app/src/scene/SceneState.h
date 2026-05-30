#pragma once

#include "asset/GarmentAsset.h"
#include "asset/MotionAsset.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using GarmentId = std::uint64_t;

struct GarmentObject {
    GarmentId id = 0;
    GarmentMesh mesh;
    bool visible = true;
};

class SceneState final {
public:
    // Character
    void set_character_mesh(CharacterMesh mesh);
    bool has_character() const;
    const CharacterMesh& character_mesh() const;

    // Garments
    GarmentId add_garment_mesh(GarmentMesh mesh);
    const std::vector<GarmentObject>& garments() const;

    // Playback
    void update_character_frame(std::uint64_t simulation_step_count,
                                std::uint32_t character_frame_stride);
    std::uint32_t current_character_frame() const;

private:
    // Character
    CharacterMesh character_mesh_;
    bool character_loaded_ = false;

    // Garments
    std::vector<GarmentObject> garments_;
    GarmentId next_garment_id_ = 1;

    // Playback
    std::uint32_t current_character_frame_ = 0;
};
