#include "simulation/SimulationScene.h"

#include <utility>

// Character // 

void SimulationScene::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    character_loaded_ = true;
    is_playing_ = true;
    current_character_frame_ = 0;
    ++character_revision_;
}

bool SimulationScene::has_character() const
{
    return character_loaded_;
}

const CharacterMesh& SimulationScene::character_mesh() const
{
    return character_mesh_;
}

std::uint64_t SimulationScene::character_revision() const
{
    return character_revision_;
}

// Garments //

GarmentId SimulationScene::add_garment_mesh(GarmentMesh mesh)
{
    const GarmentId garment_id = next_garment_id_++;
    ++garment_revision_;
    garments_.push_back({
        garment_id,
        std::move(mesh),
        true,
        garment_revision_,
    });
    return garment_id;
}

const std::vector<GarmentSceneObject>& SimulationScene::garments() const
{
    return garments_;
}

std::uint64_t SimulationScene::garment_revision() const
{
    return garment_revision_;
}

// Playback // 

bool SimulationScene::update_playback_frame(double playback_seconds)
{
    if (!character_loaded_ || !is_playing_ || character_mesh_.frame_count == 0) {
        return false;
    }

    const std::uint32_t next_frame = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(playback_seconds * character_mesh_.fps) % character_mesh_.frame_count
    );

    if (next_frame == current_character_frame_) {
        return false;
    }

    current_character_frame_ = next_frame;
    return true;
}

void SimulationScene::set_playing(bool playing)
{
    is_playing_ = playing;
}

std::uint32_t SimulationScene::current_character_frame() const
{
    return current_character_frame_;
}

bool SimulationScene::is_playing() const
{
    return is_playing_;
}
