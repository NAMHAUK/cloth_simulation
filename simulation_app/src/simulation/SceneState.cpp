#include "simulation/SceneState.h"
#include "utils/NumericUtils.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>

namespace {
glm::vec3 frame_position(const std::vector<float>& positions, std::uint32_t frame_index)
{
    const std::size_t base = frame_index * position_components;
    return {positions[base], positions[base + 1u], positions[base + 2u]};
}

glm::quat frame_orientation(const std::vector<float>& orientations, std::uint32_t frame_index)
{
    const std::size_t base = frame_index * 4u;
    return glm::normalize(quat_xyzw(orientations[base],
                                    orientations[base + 1u],
                                    orientations[base + 2u],
                                    orientations[base + 3u]));
}

glm::vec3 angular_velocity(const glm::quat& start_orientation, const glm::quat& end_orientation, float dt)
{
    glm::quat delta = glm::normalize(end_orientation * glm::conjugate(start_orientation));
    if (delta.w < 0.0f) {
        delta = -delta;
    }

    const glm::vec3 vector{delta.x, delta.y, delta.z};
    const float vector_length = glm::length(vector);

    if (vector_length <= 1.0e-8f) {
        return glm::vec3{0.0f};
    } else {
        const float angle = 2.0f * std::atan2(vector_length, delta.w);
        return vector * (angle / (vector_length * dt));
    }
}
}

// Character

void SceneState::set_character_motion(CharacterMotion motion)
{
    character_motion_ = std::move(motion);
    motion_frame_index_ = 0;

    pelvis_kinematics_.reset(frame_position(character_motion_.pelvis_positions, 0u),
                             frame_orientation(character_motion_.pelvis_orientations, 0u));
    torso_kinematics_.reset(frame_position(character_motion_.torso_positions, 0u),
                            frame_orientation(character_motion_.torso_orientations, 0u));
}

void SceneState::set_body_bvhs(Bvh triangle_bvh, Bvh edge_bvh)
{
    default_body_triangle_bvh_ = std::move(triangle_bvh);
    default_body_edge_bvh_ = std::move(edge_bvh);
}

// Motion

void SceneState::set_motion_frame_index(std::uint32_t motion_frame_index)
{
    motion_frame_index_ = std::min(motion_frame_index, character_motion_.frame_count - 1u);
}

float SceneState::motion_frame_alpha(float motion_frame_position) const
{
    return std::clamp(motion_frame_position - static_cast<float>(motion_frame_index_), 0.0f, 1.0f);
}

glm::vec3 SceneState::character_root_position(std::uint32_t motion_frame_index) const
{
    return frame_position(character_motion_.pelvis_positions, motion_frame_index);
}

// Reference Frame Kinematics

void SceneState::update_reference_frame_kinematics(float motion_frame_alpha, float dt)
{
    const auto& motion = character_motion_;
    const auto next_frame_index = std::min(motion_frame_index_ + 1u, motion.frame_count - 1u);

    auto pelvis_position = glm::mix(frame_position(motion.pelvis_positions, motion_frame_index_),
                                    frame_position(motion.pelvis_positions, next_frame_index),
                                    motion_frame_alpha);
    auto pelvis_orientation = glm::slerp(frame_orientation(motion.pelvis_orientations, motion_frame_index_),
                                         frame_orientation(motion.pelvis_orientations, next_frame_index),
                                         motion_frame_alpha);
    auto torso_position = glm::mix(frame_position(motion.torso_positions, motion_frame_index_),
                                   frame_position(motion.torso_positions, next_frame_index),
                                   motion_frame_alpha);
    auto torso_orientation = glm::slerp(frame_orientation(motion.torso_orientations, motion_frame_index_),
                                        frame_orientation(motion.torso_orientations, next_frame_index),
                                        motion_frame_alpha);

    pelvis_kinematics_.update(pelvis_position, glm::normalize(pelvis_orientation), dt);
    torso_kinematics_.update(torso_position, glm::normalize(torso_orientation), dt);
}

void ReferenceFrameKinematics::reset(const glm::vec3& position, const glm::quat& orientation)
{
    *this = {};
    start_position = end_position = position;
    orientation_ = orientation;
}

void ReferenceFrameKinematics::update(const glm::vec3& position, const glm::quat& orientation, float dt)
{
    assert(dt > 0.0f);

    start_position = end_position;
    end_position = position;
    rotation_delta = glm::mat3_cast(glm::normalize(orientation * glm::conjugate(orientation_)));

    start_linear_velocity = linear_velocity_;
    linear_velocity_ = (end_position - start_position) / dt;
    linear_acceleration = (linear_velocity_ - start_linear_velocity) / dt;

    start_angular_velocity = angular_velocity_;
    angular_velocity_ = angular_velocity(orientation_, orientation, dt);
    angular_acceleration = (angular_velocity_ - start_angular_velocity) / dt;

    orientation_ = orientation;
}

// Garments

void SceneState::set_garment(GarmentObject garment)
{
    if (GarmentObject* existing_garment = find_garment(garment.layer)) {
        *existing_garment = std::move(garment);
        return;
    }

    garments_.push_back(std::move(garment));
}

GarmentObject& SceneState::place_garment(GarmentLayer layer, const glm::vec3& position_offset, float scale)
{
    GarmentObject* garment = find_garment(layer);
    assert(garment != nullptr);

    GarmentMesh& mesh = garment->mesh;
    for (std::size_t index = 0; index < mesh.vertices.size(); index += 3u) {
        glm::vec3 position{
            mesh.vertices[index],
            mesh.vertices[index + 1u],
            mesh.vertices[index + 2u],
        };
        position = mesh.bounds_center + (position - mesh.bounds_center) * scale + position_offset;
        mesh.vertices[index] = position.x;
        mesh.vertices[index + 1u] = position.y;
        mesh.vertices[index + 2u] = position.z;
    }

    mesh.bounds_center += position_offset;
    mesh.bounds_radius *= scale;

    for (auto* constraints : {&mesh.stretch_constraints, &mesh.bending_constraints}) {
        for (float& rest_length : constraints->rest_lengths) {
            rest_length *= scale;
        }
    }

    return *garment;
}

void SceneState::update_garment_color(GarmentLayer layer, const glm::vec3& color)
{
    GarmentObject& garment = *find_garment(layer);
    garment.mesh.color = color;
}

bool SceneState::remove_garment(GarmentLayer layer)
{
    for (auto iter = garments_.begin(); iter != garments_.end(); ++iter) {
        if (iter->layer == layer) {
            garments_.erase(iter);
            return true;
        }
    }
    return false;
}

void SceneState::clear_garments()
{
    garments_.clear();
}

GarmentObject* SceneState::find_garment(GarmentLayer layer)
{
    for (GarmentObject& garment : garments_) {
        if (garment.layer == layer) {
            return &garment;
        }
    }
    return nullptr;
}

const GarmentObject* SceneState::find_garment(GarmentLayer layer) const
{
    for (const GarmentObject& garment : garments_) {
        if (garment.layer == layer) {
            return &garment;
        }
    }
    return nullptr;
}

// Accessors

const CharacterMotion& SceneState::character_motion() const
{
    return character_motion_;
}

const Bvh& SceneState::default_body_triangle_bvh() const
{
    return default_body_triangle_bvh_;
}

const Bvh& SceneState::default_body_edge_bvh() const
{
    return default_body_edge_bvh_;
}

const std::vector<GarmentObject>& SceneState::garments() const
{
    return garments_;
}

const ReferenceFrameKinematics& SceneState::reference_frame_kinematics(GarmentCategory category) const
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

std::uint32_t SceneState::motion_frame_index() const
{
    return motion_frame_index_;
}
