#include "scene/SceneState.h"

#include "asset/MeshGeometryUtils.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <iostream>
#include <utility>

// Character // 

void SceneState::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    current_character_frame_ = 0;
}

void SceneState::set_default_character_bvh_data(MeshBvhData default_character_bvh_data)
{
    default_character_bvh_data_ = std::move(default_character_bvh_data);
}

const CharacterMesh& SceneState::character_mesh() const
{
    return character_mesh_;
}

const MeshBvhData& SceneState::default_character_bvh_data() const
{
    return default_character_bvh_data_;
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
    GarmentMesh source_mesh = mesh;
    garments_.push_back({
        garment_id,
        std::move(source_mesh),
        std::move(mesh),
        {},
        true,
    });
    return garment_id;
}

GarmentObject* SceneState::update_garment_placement(std::uint32_t garment_id, const glm::vec3& position_offset, float scale)
{
    if (scale <= 0.0f) {
        return nullptr;
    }

    for (GarmentObject& garment : garments_) {
        if (garment.id != garment_id) {
            continue;
        }

        const GarmentMesh& source_mesh = garment.source_mesh;
        GarmentMesh next_mesh = source_mesh;
        const glm::vec3 scale_center = source_mesh.bounds_center;

        for (std::size_t index = 0; index < next_mesh.vertices.size(); index += 3u) {
            const glm::vec3 source_position{
                source_mesh.vertices[index],
                source_mesh.vertices[index + 1u],
                source_mesh.vertices[index + 2u],
            };
            const glm::vec3 next_position = scale_center + (source_position - scale_center) * scale + position_offset;
            next_mesh.vertices[index] = next_position.x;
            next_mesh.vertices[index + 1u] = next_position.y;
            next_mesh.vertices[index + 2u] = next_position.z;
        }

        next_mesh.bounds_center = source_mesh.bounds_center + position_offset;
        next_mesh.bounds_radius = source_mesh.bounds_radius * scale;

        for (std::size_t index = 0; index < next_mesh.stretch_constraints.rest_lengths.size(); ++index) {
            next_mesh.stretch_constraints.rest_lengths[index] =
                source_mesh.stretch_constraints.rest_lengths[index] * scale;
        }
        for (std::size_t index = 0; index < next_mesh.bending_constraints.rest_lengths.size(); ++index) {
            next_mesh.bending_constraints.rest_lengths[index] =
                source_mesh.bending_constraints.rest_lengths[index] * scale;
        }

        garment.mesh = std::move(next_mesh);
        garment.attachment_constraints.clear();
        return &garment;
    }

    return nullptr;
}

bool SceneState::remove_garment(std::uint32_t garment_id)
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(),
        [garment_id](const GarmentObject& garment) {
            return garment.id == garment_id;
        }
    );

    if (iter == garments_.end()) {
        return false;
    }

    garments_.erase(iter);
    return true;
}

GarmentObject* SceneState::find_garment(std::uint32_t garment_id)
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(),
        [garment_id](const GarmentObject& garment) {
            return garment.id == garment_id;
        }
    );

    if (iter == garments_.end()) {
        return nullptr;
    }

    return &(*iter);
}

void SceneState::clear_garments()
{
    garments_.clear();
    next_garment_id_ = 1;
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
    if (character_mesh_.frame_count == 0) {
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
