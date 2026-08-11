#include "scene/SceneState.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::size_t position_components = 3u;
constexpr std::size_t quaternion_components = 4u;

glm::vec3 frame_position(const std::vector<float>& positions, std::uint32_t frame_index)
{
    const std::size_t base = static_cast<std::size_t>(frame_index) * position_components;
    if (positions.size() < base + position_components) {
        return glm::vec3{0.0f};
    }
    return {positions[base], positions[base + 1u], positions[base + 2u]};
}

glm::quat frame_orientation(const std::vector<float>& orientations, std::uint32_t frame_index)
{
    const std::size_t base = static_cast<std::size_t>(frame_index) * quaternion_components;
    if (orientations.size() < base + quaternion_components) {
        return glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return glm::normalize(glm::quat::wxyz(orientations[base + 3u],
                                          orientations[base],
                                          orientations[base + 1u],
                                          orientations[base + 2u]));
}

}

// Character //

void SceneState::set_character_mesh(CharacterMesh mesh)
{
    character_mesh_ = std::move(mesh);
    current_character_frame_ = 0;

    const auto pelvis = interpolated_character_reference_frame(0.0f, GarmentCategory::Bottom);
    const auto torso = interpolated_character_reference_frame(0.0f, GarmentCategory::Top);
    pelvis_kinematics_.reset(pelvis);
    torso_kinematics_.reset(torso);
}

void SceneState::set_body_bvhs(TriangleBvhData triangle_bvh, VertexBvhData vertex_bvh, EdgeBvhData edge_bvh)
{
    default_body_triangle_bvh_data_ = std::move(triangle_bvh);
    default_body_vertex_bvh_data_ = std::move(vertex_bvh);
    default_body_edge_bvh_data_ = std::move(edge_bvh);
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

void SceneState::set_garment(GarmentObject garment)
{
    GarmentObject* existing_garment = find_garment(garment.layer);
    if (existing_garment != nullptr) {
        *existing_garment = std::move(garment);
        return;
    }

    garments_.push_back(std::move(garment));
    std::sort(garments_.begin(), garments_.end(), [](const GarmentObject& lhs, const GarmentObject& rhs) {
        return lhs.layer < rhs.layer;
    });
}

GarmentObject& SceneState::apply_garment_placement(GarmentLayer layer,
                                                   const glm::vec3& position_offset,
                                                   float scale)
{
    GarmentObject* garment = find_garment(layer);
    if (garment == nullptr || scale <= 0.0f) {
        throw std::runtime_error("Cannot apply garment placement.");
    }

    const GarmentMesh& source_mesh = garment->source_mesh;
    GarmentMesh next_mesh = source_mesh;
    const glm::vec3 scale_center = source_mesh.bounds_center;

    for (std::size_t index = 0; index < next_mesh.vertices.size(); index += 3u) {
        const glm::vec3 source_position{
            source_mesh.vertices[index],
            source_mesh.vertices[index + 1u],
            source_mesh.vertices[index + 2u],
        };
        const glm::vec3 next_position =
            scale_center + (source_position - scale_center) * scale + position_offset;
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

    garment->mesh = std::move(next_mesh);
    return *garment;
}

void SceneState::update_garment_color(GarmentLayer layer, const glm::vec3& color)
{
    GarmentObject& garment = *find_garment(layer);
    garment.source_mesh.color = color;
    garment.mesh.color = color;
}

bool SceneState::remove_garment(GarmentLayer layer)
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(), [layer](const GarmentObject& garment) {
        return garment.layer == layer;
    });

    if (iter == garments_.end()) {
        return false;
    }

    garments_.erase(iter);
    return true;
}

GarmentObject* SceneState::find_garment(GarmentLayer layer)
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(), [layer](const GarmentObject& garment) {
        return garment.layer == layer;
    });

    if (iter == garments_.end()) {
        return nullptr;
    }

    return &(*iter);
}

const GarmentObject* SceneState::find_garment(GarmentLayer layer) const
{
    const auto iter = std::find_if(garments_.begin(), garments_.end(), [layer](const GarmentObject& garment) {
        return garment.layer == layer;
    });
    return iter == garments_.end() ? nullptr : &(*iter);
}

void SceneState::clear_garments()
{
    garments_.clear();
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
void SceneState::update_character_frame(std::uint64_t frame_index)
{
    if (character_mesh_.frame_count == 0) {
        return;
    }

    const std::uint32_t last_frame_index = character_mesh_.frame_count - 1u;

    current_character_frame_ =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(frame_index, last_frame_index));
}

void SceneState::update_reference_kinematics(float frame_alpha, float dt)
{
    const auto pelvis = interpolated_character_reference_frame(frame_alpha, GarmentCategory::Bottom);
    const auto torso = interpolated_character_reference_frame(frame_alpha, GarmentCategory::Top);
    pelvis_kinematics_.update(pelvis, dt);
    torso_kinematics_.update(torso, dt);
}

const Kinematics& SceneState::reference_kinematics(GarmentCategory garment_category) const
{
    return garment_category == GarmentCategory::Top ? torso_kinematics_ : pelvis_kinematics_;
}

float SceneState::character_frame_alpha(float character_frame_time) const
{
    if (character_mesh_.frame_count == 0) {
        return 0.0f;
    }

    // motion이 종료된 경우 값 고정
    const std::uint32_t last_frame_index = character_mesh_.frame_count - 1u;
    if (current_character_frame_ >= last_frame_index) {
        return 0.0f;
    }

    return std::clamp(character_frame_time - static_cast<float>(current_character_frame_), 0.0f, 1.0f);
}

CharacterReferenceFrame SceneState::interpolated_character_reference_frame(
    float frame_alpha,
    GarmentCategory garment_category) const
{
    const bool uses_torso = garment_category == GarmentCategory::Top;
    const std::vector<float>& positions =
        uses_torso ? character_mesh_.torso_positions : character_mesh_.root_positions;
    const std::vector<float>& orientations =
        uses_torso ? character_mesh_.torso_orientations : character_mesh_.pelvis_orientations;
    const std::uint32_t last_frame_index =
        character_mesh_.frame_count > 0 ? character_mesh_.frame_count - 1u : 0u;
    const std::uint32_t next_frame_index = std::min(current_character_frame_ + 1u, last_frame_index);
    const glm::vec3 current_position = frame_position(positions, current_character_frame_);
    const glm::vec3 next_position = frame_position(positions, next_frame_index);
    const glm::quat current_orientation = frame_orientation(orientations, current_character_frame_);
    glm::quat next_orientation = frame_orientation(orientations, next_frame_index);
    if (glm::dot(current_orientation, next_orientation) < 0.0f) {
        next_orientation = -next_orientation;
    }

    return {current_position + (next_position - current_position) * frame_alpha,
            glm::normalize(glm::slerp(current_orientation, next_orientation, frame_alpha))};
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
