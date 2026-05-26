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
    std::uint64_t revision = 0;
};

class SceneState final {
public:
    // Character
    void set_character_mesh(CharacterMesh mesh);
    bool has_character() const;
    const CharacterMesh& character_mesh() const;
    std::uint64_t character_revision() const;

    // Garments
    GarmentId add_garment_mesh(GarmentMesh mesh);
    const std::vector<GarmentObject>& garments() const;
    std::uint64_t garment_revision() const;

    // Playback
    bool update_playback_frame(double playback_seconds);
    void set_playing(bool playing);
    std::uint32_t current_character_frame() const;
    bool is_playing() const;

private:
    // Character
    CharacterMesh character_mesh_;
    bool character_loaded_ = false;
    std::uint64_t character_revision_ = 0;

    // Garments
    std::vector<GarmentObject> garments_;
    GarmentId next_garment_id_ = 1;
    std::uint64_t garment_revision_ = 0;

    // Playback
    bool is_playing_ = false;
    std::uint32_t current_character_frame_ = 0;
};
