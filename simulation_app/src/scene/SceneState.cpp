#include "scene/SceneState.h"

#include "asset/MeshGeometryUtils.h"

#include <iostream>
#include <utility>

// Character // 

void SceneState::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    character_loaded_ = true;
    current_character_frame_ = 0;
}

bool SceneState::has_character() const
{
    return character_loaded_;
}

const CharacterMesh& SceneState::character_mesh() const
{
    return character_mesh_;
}

// Garments //

std::uint32_t SceneState::add_garment_mesh(GarmentMesh mesh)
{
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / 3u);
    if (!mesh.adjacency.is_valid(vertex_count) &&
        !build_vertex_face_adjacency(vertex_count, mesh.indices, mesh.adjacency)) {
        std::cerr << "Cannot add garment mesh with invalid topology.\n";
    }

    const std::uint32_t garment_id = next_garment_id_++;
    garments_.push_back({
        garment_id,
        std::move(mesh),
        true,
    });
    return garment_id;
}

const std::vector<GarmentObject>& SceneState::garments() const
{
    return garments_;
}

// Playback // 

// cloth 60fps, character 30fps 기준, 2step마다 character frame update
void SceneState::update_character_frame(std::uint64_t simulation_step_count,
                                        std::uint32_t character_frame_stride)
{
    if (!character_loaded_ || character_mesh_.frame_count == 0) {
        return;
    }

    const std::uint64_t character_step = simulation_step_count / character_frame_stride;
    const std::uint32_t next_frame = static_cast<std::uint32_t>(character_step % character_mesh_.frame_count);

    current_character_frame_ = next_frame;
}

std::uint32_t SceneState::current_character_frame() const
{
    return current_character_frame_;
}
