#include "scene/SceneState.h"

#include <utility>

// Character // 

void SceneState::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    character_loaded_ = true;
    is_playing_ = true;
    current_character_frame_ = 0;
    ++character_revision_;
}

bool SceneState::has_character() const
{
    return character_loaded_;
}

const CharacterMesh& SceneState::character_mesh() const
{
    return character_mesh_;
}

std::uint64_t SceneState::character_revision() const
{
    return character_revision_;
}

// Garments //

GarmentId SceneState::add_garment_mesh(GarmentMesh mesh)
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

const std::vector<GarmentSceneObject>& SceneState::garments() const
{
    return garments_;
}

std::uint64_t SceneState::garment_revision() const
{
    return garment_revision_;
}

// Playback // 

bool SceneState::update_playback_frame(double playback_seconds)
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

void SceneState::set_playing(bool playing)
{
    is_playing_ = playing;
}

std::uint32_t SceneState::current_character_frame() const
{
    return current_character_frame_;
}

bool SceneState::is_playing() const
{
    return is_playing_;
}
