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
    return {positions[base], positions[base + 1u], positions[base + 2u]};
}

glm::quat frame_orientation(const std::vector<float>& orientations, std::uint32_t frame_index)
{
    const std::size_t base = static_cast<std::size_t>(frame_index) * quaternion_components;
    return glm::normalize(glm::quat::wxyz(orientations[base + 3u],
                                          orientations[base],
                                          orientations[base + 1u],
                                          orientations[base + 2u]));
}
}

// Character //

void SceneState::set_character_motion(CharacterMotion motion)
{
    character_motion_ = std::move(motion);
    motion_frame_index_ = 0;

    const auto pelvis = reference_frame(0u, GarmentCategory::Bottom);
    const auto torso = reference_frame(0u, GarmentCategory::Top);
    pelvis_kinematics_.reset(pelvis);
    torso_kinematics_.reset(torso);
}

void SceneState::set_body_bvhs(TriangleBvhData triangle_bvh, VertexBvhData vertex_bvh, EdgeBvhData edge_bvh)
{
    default_body_triangle_bvh_data_ = std::move(triangle_bvh);
    default_body_vertex_bvh_data_ = std::move(vertex_bvh);
    default_body_edge_bvh_data_ = std::move(edge_bvh);
}

const CharacterMotion& SceneState::character_motion() const
{
    return character_motion_;
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
    if (GarmentObject* existing_garment = find_garment(garment.layer)) {
        *existing_garment = std::move(garment);
        return;
    }

    garments_.push_back(std::move(garment));
}

GarmentObject& SceneState::apply_garment_placement(GarmentLayer layer,
                                                   const glm::vec3& position_offset,
                                                   float scale)
{
    GarmentObject* garment = find_garment(layer);
    GarmentMesh next_mesh = garment->source_mesh;
    const glm::vec3 scale_center = next_mesh.bounds_center;

    for (std::size_t index = 0; index < next_mesh.vertices.size(); index += 3u) {
        glm::vec3 position{
            next_mesh.vertices[index],
            next_mesh.vertices[index + 1u],
            next_mesh.vertices[index + 2u],
        };
        position = scale_center + (position - scale_center) * scale + position_offset;
        next_mesh.vertices[index] = position.x;
        next_mesh.vertices[index + 1u] = position.y;
        next_mesh.vertices[index + 2u] = position.z;
    }

    next_mesh.bounds_center += position_offset;
    next_mesh.bounds_radius *= scale;

    for (GarmentDistanceConstraints* constraints :
         {&next_mesh.stretch_constraints, &next_mesh.bending_constraints}) {
        for (float& rest_length : constraints->rest_lengths) {
            rest_length *= scale;
        }
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

    return iter == garments_.end() ? nullptr : &(*iter);
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

// Playback //
void SceneState::set_motion_frame_index(std::uint64_t motion_frame_index)
{
    const std::uint32_t last_frame_index = character_motion_.frame_count - 1u;

    motion_frame_index_ =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(motion_frame_index, last_frame_index));
}

void SceneState::update_reference_frame_kinematics(float motion_frame_alpha, float dt)
{
    const auto pelvis = interpolated_reference_frame(motion_frame_alpha, GarmentCategory::Bottom);
    const auto torso = interpolated_reference_frame(motion_frame_alpha, GarmentCategory::Top);
    pelvis_kinematics_.update(pelvis, dt);
    torso_kinematics_.update(torso, dt);
}

const Kinematics& SceneState::reference_frame_kinematics(GarmentCategory category) const
{
    switch (category) {
    case GarmentCategory::Top:
        return torso_kinematics_;

    case GarmentCategory::Bottom:
    case GarmentCategory::FullBody:
        return pelvis_kinematics_;
    }

    throw std::runtime_error("Unsupported garment category.");
}

float SceneState::motion_frame_alpha(float motion_frame_position) const
{
    return std::clamp(motion_frame_position - static_cast<float>(motion_frame_index_), 0.0f, 1.0f);
}

CharacterReferenceFrame SceneState::interpolated_reference_frame(float motion_frame_alpha,
                                                                 GarmentCategory category) const
{
    const auto next_frame_index = std::min(motion_frame_index_ + 1u, character_motion_.frame_count - 1u);
    const auto current = reference_frame(motion_frame_index_, category);
    const auto next = reference_frame(next_frame_index, category);

    return {glm::mix(current.position, next.position, motion_frame_alpha),
            glm::normalize(glm::slerp(current.orientation, next.orientation, motion_frame_alpha))};
}

CharacterReferenceFrame SceneState::reference_frame(std::uint32_t motion_frame_index,
                                                    GarmentCategory category) const
{
    switch (category) {
    case GarmentCategory::Top:
        return {frame_position(character_motion_.torso_positions, motion_frame_index),
                frame_orientation(character_motion_.torso_orientations, motion_frame_index)};

    case GarmentCategory::Bottom:
    case GarmentCategory::FullBody:
        return {frame_position(character_motion_.pelvis_positions, motion_frame_index),
                frame_orientation(character_motion_.pelvis_orientations, motion_frame_index)};
    }

    throw std::runtime_error("Unsupported garment category.");
}

std::uint32_t SceneState::motion_frame_index() const
{
    return motion_frame_index_;
}

glm::vec3 SceneState::character_root_position(std::uint32_t motion_frame_index) const
{
    if (motion_frame_index >= character_motion_.frame_count) {
        return glm::vec3{0.0f};
    }

    return frame_position(character_motion_.pelvis_positions, motion_frame_index);
}
