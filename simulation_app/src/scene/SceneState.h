#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhDataTypes.h"
#include "scene/Kinematics.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct GarmentObject
{
    GarmentLayer layer = GarmentLayer::Lower;
    GarmentMesh mesh;
    TriangleBvhData triangle_bvh;
};

class SceneState final
{
public:
    void set_character_motion(CharacterMotion motion);
    void set_body_bvhs(TriangleBvhData triangle_bvh, VertexBvhData vertex_bvh, EdgeBvhData edge_bvh);

    void set_motion_frame_index(std::uint32_t motion_frame_index);
    float motion_frame_alpha(float motion_frame_position) const;
    void update_reference_frame_kinematics(float motion_frame_alpha, float dt);
    glm::vec3 character_root_position(std::uint32_t motion_frame_index) const;

    void set_garment(GarmentObject garment);
    GarmentObject& apply_garment_placement(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void update_garment_color(GarmentLayer layer, const glm::vec3& color);
    bool remove_garment(GarmentLayer layer);
    void clear_garments();
    GarmentObject* find_garment(GarmentLayer layer);
    const GarmentObject* find_garment(GarmentLayer layer) const;

    const CharacterMotion& character_motion() const;
    const TriangleBvhData& default_body_triangle_bvh_data() const;
    const VertexBvhData& default_body_vertex_bvh_data() const;
    const EdgeBvhData& default_body_edge_bvh_data() const;
    const std::vector<GarmentObject>& garments() const;
    const Kinematics& reference_frame_kinematics(GarmentCategory category) const;
    std::uint32_t motion_frame_index() const;

private:
    CharacterReferenceFrame interpolated_reference_frame(float motion_frame_alpha,
                                                         GarmentCategory category) const;
    CharacterReferenceFrame reference_frame(std::uint32_t motion_frame_index, GarmentCategory category) const;

    CharacterMotion character_motion_;
    TriangleBvhData default_body_triangle_bvh_data_;
    VertexBvhData default_body_vertex_bvh_data_;
    EdgeBvhData default_body_edge_bvh_data_;
    std::vector<GarmentObject> garments_;
    std::uint32_t motion_frame_index_ = 0;
    Kinematics pelvis_kinematics_;
    Kinematics torso_kinematics_;
};
