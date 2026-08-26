#pragma once

#include "asset/AssetDataTypes.h"
#include "simulation/collision/Bvh.h"
#include "utils/NumericUtils.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

struct ReferenceFrameKinematics final
{
    void reset(const glm::vec3& position, const glm::quat& orientation);
    void update(const glm::vec3& position, const glm::quat& orientation, float dt);

    glm::vec3 start_position{};
    glm::vec3 end_position{};
    glm::mat3 rotation_delta{1.0f};
    glm::vec3 start_velocity{};
    glm::vec3 acceleration{};
    glm::vec3 start_angular_velocity{};
    glm::vec3 angular_acceleration{};

private:
    glm::quat orientation_ = quat_xyzw(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 velocity_{};
    glm::vec3 angular_velocity_{};
};

struct GarmentObject
{
    GarmentLayer layer = GarmentLayer::Lower;
    GarmentMesh mesh;
    Bvh triangle_bvh;
};

class SceneState final
{
public:
    void set_character_motion(CharacterMotion motion);
    void set_body_bvhs(Bvh triangle_bvh, Bvh vertex_bvh, Bvh edge_bvh);

    void set_motion_frame_index(std::uint32_t motion_frame_index);
    float motion_frame_alpha(float motion_frame_position) const;
    void update_reference_frame_kinematics(float motion_frame_alpha, float dt);
    glm::vec3 character_root_position(std::uint32_t motion_frame_index) const;

    void set_garment(GarmentObject garment);
    GarmentObject& place_garment(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void update_garment_color(GarmentLayer layer, const glm::vec3& color);
    bool remove_garment(GarmentLayer layer);
    void clear_garments();
    GarmentObject* find_garment(GarmentLayer layer);
    const GarmentObject* find_garment(GarmentLayer layer) const;

    const CharacterMotion& character_motion() const;
    const Bvh& default_body_triangle_bvh() const;
    const Bvh& default_body_vertex_bvh() const;
    const Bvh& default_body_edge_bvh() const;
    const std::vector<GarmentObject>& garments() const;
    const ReferenceFrameKinematics& reference_frame_kinematics(GarmentCategory category) const;
    std::uint32_t motion_frame_index() const;

private:
    CharacterMotion character_motion_;
    Bvh default_body_triangle_bvh_;
    Bvh default_body_vertex_bvh_;
    Bvh default_body_edge_bvh_;
    std::vector<GarmentObject> garments_;
    std::uint32_t motion_frame_index_ = 0;
    ReferenceFrameKinematics pelvis_kinematics_;
    ReferenceFrameKinematics torso_kinematics_;
};
