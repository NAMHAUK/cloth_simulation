#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhDataTypes.h"
#include "scene/Kinematics.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

struct GarmentObject
{
    GarmentLayer layer = GarmentLayer::Lower;
    GarmentMesh source_mesh;
    GarmentMesh mesh;
    bool visible = true;
    std::optional<TriangleBvhData> garment_triangle_bvh;
};

struct CharacterReferenceFrame final
{
    glm::vec3 position{};
    glm::quat orientation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
};

class SceneState final
{
public:
    // Character
    void set_character_mesh(CharacterMesh mesh);
    void set_body_bvhs(TriangleBvhData triangle_bvh, VertexBvhData vertex_bvh, EdgeBvhData edge_bvh);
    const CharacterMesh& character_mesh() const;
    const TriangleBvhData& default_body_triangle_bvh_data() const;
    const VertexBvhData& default_body_vertex_bvh_data() const;
    const EdgeBvhData& default_body_edge_bvh_data() const;

    // Garments
    bool add_garment_mesh(GarmentLayer layer, GarmentMesh mesh);
    bool replace_garment_mesh(GarmentLayer layer, GarmentMesh mesh);
    bool remove_garment(GarmentLayer layer);
    GarmentObject* update_garment_placement(GarmentLayer layer,
                                            const glm::vec3& position_offset,
                                            float scale);
    bool update_garment_color(GarmentLayer layer, const glm::vec3& color);
    GarmentObject* find_garment(GarmentLayer layer);
    const GarmentObject* find_garment(GarmentLayer layer) const;
    void clear_garments();
    const std::vector<GarmentObject>& garments() const;
    bool has_multiple_garments() const;

    // Playback
    void update_character_frame(std::uint64_t frame_index);
    void update_reference_kinematics(float frame_alpha, float dt);
    const Kinematics& reference_kinematics(GarmentCategory garment_category) const;
    float character_frame_alpha(float character_frame_time) const;
    CharacterReferenceFrame interpolated_character_reference_frame(float frame_alpha,
                                                                   GarmentCategory garment_category) const;
    std::uint32_t current_character_frame() const;
    glm::vec3 character_root_position(std::uint32_t frame_index) const;

private:
    // Character
    CharacterMesh character_mesh_;
    TriangleBvhData default_body_triangle_bvh_data_;
    VertexBvhData default_body_vertex_bvh_data_;
    EdgeBvhData default_body_edge_bvh_data_;

    // Garments
    std::vector<GarmentObject> garments_;

    // Playback
    std::uint32_t current_character_frame_ = 0;
    Kinematics pelvis_kinematics_;
    Kinematics torso_kinematics_;
};
