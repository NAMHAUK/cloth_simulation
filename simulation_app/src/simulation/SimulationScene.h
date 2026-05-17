#pragma once

#include "assets/GarmentAsset.h"
#include "assets/MotionAsset.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct GarmentSceneObject {
    GarmentMesh mesh;
    bool visible = true;
    std::uint64_t revision = 0;
};

class SimulationScene final {
public:
    void set_character_mesh(CharacterMesh mesh);
    std::size_t add_garment_mesh(GarmentMesh mesh);

    bool update_playback_frame(double elapsed_seconds);
    void set_playing(bool playing);

    bool has_character() const;
    const CharacterMesh& character_mesh() const;
    std::uint32_t current_character_frame() const;
    bool is_playing() const;
    std::uint64_t character_revision() const;

    const std::vector<GarmentSceneObject>& garments() const;
    std::uint64_t garment_revision() const;

private:
    CharacterMesh character_mesh_;
    bool character_loaded_ = false;
    bool is_playing_ = false;
    std::uint32_t current_character_frame_ = 0;
    std::uint64_t character_revision_ = 0;

    std::vector<GarmentSceneObject> garments_;
    std::uint64_t garment_revision_ = 0;
};
