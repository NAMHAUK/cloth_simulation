#pragma once

#include "asset/AssetDataTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct GarmentObject {
    std::uint32_t id = 0;
    GarmentMesh mesh;
    bool visible = true;
};

class SceneState final {
public:
    // Character
    void set_character_mesh(CharacterMesh mesh);
    const CharacterMesh& character_mesh() const;

    // Garments
    std::uint32_t add_garment_mesh(GarmentMesh mesh);
    const std::vector<GarmentObject>& garments() const;

    // Playback
    void update_character_frame(std::uint64_t simulation_step_count,
                                std::uint32_t character_frame_stride);
    std::uint32_t current_character_frame() const;

private:
    // Character
    CharacterMesh character_mesh_;

    // Garments
    std::vector<GarmentObject> garments_;
    std::uint32_t next_garment_id_ = 1;

    // Playback
    std::uint32_t current_character_frame_ = 0;
};
