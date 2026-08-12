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
    GarmentMesh source_mesh;
    GarmentMesh mesh;
    TriangleBvhData triangle_bvh;
};

class SceneState final
{
public:
    // Character
    void set_character_motion(CharacterMotion motion);
    void set_body_bvhs(TriangleBvhData triangle_bvh, VertexBvhData vertex_bvh, EdgeBvhData edge_bvh);
    const CharacterMotion& character_motion() const;
    const TriangleBvhData& default_body_triangle_bvh_data() const;
    const VertexBvhData& default_body_vertex_bvh_data() const;
    const EdgeBvhData& default_body_edge_bvh_data() const;

    // Garments
    void set_garment(GarmentObject garment);
    bool remove_garment(GarmentLayer layer);
    GarmentObject& apply_garment_placement(GarmentLayer layer, const glm::vec3& position_offset, float scale);
    void update_garment_color(GarmentLayer layer, const glm::vec3& color);
    GarmentObject* find_garment(GarmentLayer layer);
    const GarmentObject* find_garment(GarmentLayer layer) const;
    void clear_garments();
    const std::vector<GarmentObject>& garments() const;
    // Playback
    void set_motion_frame_index(std::uint32_t motion_frame_index);
    void update_reference_frame_kinematics(float motion_frame_alpha, float dt);
    const Kinematics& reference_frame_kinematics(GarmentCategory category) const;
    float motion_frame_alpha(float motion_frame_position) const;
    std::uint32_t motion_frame_index() const;
    glm::vec3 character_root_position(std::uint32_t motion_frame_index) const;

private:
    // Character
    CharacterMotion character_motion_;
    TriangleBvhData default_body_triangle_bvh_data_;
    VertexBvhData default_body_vertex_bvh_data_;
    EdgeBvhData default_body_edge_bvh_data_;

    // Garments
    std::vector<GarmentObject> garments_;

    // Playback
    CharacterReferenceFrame interpolated_reference_frame(float motion_frame_alpha, GarmentCategory category) const;
    CharacterReferenceFrame reference_frame(std::uint32_t motion_frame_index, GarmentCategory category) const;
    std::uint32_t motion_frame_index_ = 0;
    Kinematics pelvis_kinematics_;
    Kinematics torso_kinematics_;
};
