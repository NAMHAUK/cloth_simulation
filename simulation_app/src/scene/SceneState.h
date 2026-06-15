#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/collision/BvhDataTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct GarmentObject {
    std::uint32_t id = 0;
    GarmentMesh source_mesh;
    GarmentMesh mesh;
    std::vector<GarmentAttachmentConstraint> attachment_constraints;
    bool visible = true;
};

class SceneState final {
public:
    // Character
    void set_character_mesh(CharacterMesh mesh);
    void set_default_character_bvh_data(MeshBvhData default_character_bvh_data);
    const CharacterMesh& character_mesh() const;
    const MeshBvhData& default_character_bvh_data() const;

    // Garments
    std::uint32_t add_garment_mesh(GarmentMesh mesh);
    bool remove_garment(std::uint32_t garment_id);
    GarmentObject* update_garment_placement(std::uint32_t garment_id, const glm::vec3& position_offset, float scale);
    GarmentObject* find_garment(std::uint32_t garment_id);
    void clear_garments();
    const std::vector<GarmentObject>& garments() const;

    // Playback
    void update_character_frame(std::uint64_t simulation_step_count, std::uint32_t character_frame_stride);
    std::uint32_t current_character_frame() const;
    glm::vec3 character_root_position(std::uint32_t frame_index) const;

private:
    // Character
    CharacterMesh character_mesh_;
    MeshBvhData default_character_bvh_data_;

    // Garments
    std::vector<GarmentObject> garments_;
    std::uint32_t next_garment_id_ = 1;

    // Playback
    std::uint32_t current_character_frame_ = 0;
};
