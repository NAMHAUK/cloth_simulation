#include "scene/SceneState.h"

#include "asset/MeshGeometryUtils.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <iostream>
#include <utility>

namespace {
bool prepare_garment_mesh(GarmentMesh& mesh)
{
    const std::uint32_t vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / 3u);
    std::uint32_t flipped_triangle_count = 0u;
    if (!orient_triangle_winding_outward(vertex_count,
                                         mesh.vertices,
                                         mesh.bounds_center,
                                         mesh.triangle_vertex_indices,
                                         flipped_triangle_count)) {
        std::cerr << "Cannot prepare garment mesh with inconsistent triangle winding.\n";
        return false;
    }
    if (flipped_triangle_count > 0u) {
        std::cerr << "Oriented garment triangle winding: flipped "
                  << flipped_triangle_count << " triangles.\n";
    }

    if (!mesh.adjacency.is_valid(vertex_count) &&
        !build_vertex_face_adjacency(vertex_count, mesh.triangle_vertex_indices, mesh.adjacency)) {
        std::cerr << "Cannot prepare garment mesh with invalid topology.\n";
        return false;
    }

    mesh.color = glm::vec3{1.0f};
    return true;
}
}

// Character // 

void SceneState::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    current_character_frame_ = 0;
}

void SceneState::set_default_body_triangle_bvh_data(TriangleBvhData default_body_triangle_bvh_data)
{
    default_body_triangle_bvh_data_ = std::move(default_body_triangle_bvh_data);
}

void SceneState::set_default_body_vertex_bvh_data(VertexBvhData default_body_vertex_bvh_data)
{
    default_body_vertex_bvh_data_ = std::move(default_body_vertex_bvh_data);
}

void SceneState::set_default_body_edge_bvh_data(EdgeBvhData default_body_edge_bvh_data)
{
    default_body_edge_bvh_data_ = std::move(default_body_edge_bvh_data);
}

const CharacterMesh& SceneState::character_mesh() const
{
    return character_mesh_;
}

const TriangleBvhData& SceneState::default_body_triangle_bvh_data() const
{
    return default_body_triangle_bvh_data_;
}

const VertexBvhData& SceneState::default_body_vertex_bvh_data() const
{
    return default_body_vertex_bvh_data_;
}

const EdgeBvhData& SceneState::default_body_edge_bvh_data() const
{
    return default_body_edge_bvh_data_;
}

// Garments //

std::uint32_t SceneState::add_garment_mesh(GarmentMesh mesh)
{
    if (!prepare_garment_mesh(mesh)) {
        return 0u;
    }

    const std::uint32_t garment_id = next_garment_id_++;
    // Garment insertion order defines lower-to-upper GPU buffer placement.
    const std::uint32_t garment_layer = next_garment_layer_++;
    GarmentMesh source_mesh = mesh;
    garments_.push_back({
        garment_id,
        garment_layer,
        std::move(source_mesh),
        std::move(mesh),
        true,
    });
    return garment_id;
}

bool SceneState::replace_garment_mesh(std::uint32_t garment_id, GarmentMesh mesh)
{
    GarmentObject* garment = find_garment(garment_id);
    if (garment == nullptr) {
        return false;
    }

    if (!prepare_garment_mesh(mesh)) {
        return false;
    }
    GarmentMesh source_mesh = mesh;
    garment->source_mesh = std::move(source_mesh);
    garment->mesh = std::move(mesh);
    garment->garment_triangle_bvh.reset();
    return true;
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
        return &garment;
    }

    return nullptr;
}

bool SceneState::update_garment_color(std::uint32_t garment_id, const glm::vec3& color)
{
    GarmentObject* garment = find_garment(garment_id);
    if (garment == nullptr) {
        return false;
    }

    garment->source_mesh.color = color;
    garment->mesh.color = color;
    return true;
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
    next_garment_layer_ = 0;
}

const std::vector<GarmentObject>& SceneState::garments() const
{
    return garments_;
}

bool SceneState::has_multiple_garments() const
{
    return garments_.size() >= 2u;
}

// Playback // 
void SceneState::update_character_frame(std::uint64_t simulation_step_count,
                                        std::uint32_t character_frame_stride)
{
    if (character_mesh_.frame_count == 0 || character_frame_stride == 0) {
        return;
    }

    const std::uint64_t frame_index = simulation_step_count / character_frame_stride;
    const std::uint32_t last_frame_index = character_mesh_.frame_count - 1u;

    current_character_frame_ = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(frame_index, last_frame_index)
    );
}

CharacterFrameInterpolation SceneState::character_frame_interpolation(float character_frame_time) const
{
    if (character_mesh_.frame_count == 0) {
        return {};
    }

    // motion이 종료된 경우 값 고정
    const std::uint32_t last_frame_index = character_mesh_.frame_count - 1u;
    if (character_frame_time >= last_frame_index) {
        return {last_frame_index, last_frame_index, 0.0f};
    }

    const std::uint32_t current_frame_index = static_cast<std::uint32_t>(character_frame_time);
    const std::uint32_t next_frame_index = current_frame_index + 1u;
    const float frame_alpha = character_frame_time - current_frame_index;
    return {current_frame_index, next_frame_index, frame_alpha};
}

std::uint32_t SceneState::current_character_frame() const
{
    return current_character_frame_;
}

glm::vec3 SceneState::character_root_position(std::uint32_t frame_index) const
{
    if (frame_index >= character_mesh_.frame_count ||
        character_mesh_.root_positions.size() < (static_cast<std::size_t>(frame_index) + 1u) * 3u) {
        return glm::vec3{0.0f};
    }

    const std::size_t root_base = static_cast<std::size_t>(frame_index) * 3u;
    return {
        character_mesh_.root_positions[root_base],
        character_mesh_.root_positions[root_base + 1u],
        character_mesh_.root_positions[root_base + 2u],
    };
}
