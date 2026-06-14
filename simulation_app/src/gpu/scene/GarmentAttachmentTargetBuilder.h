#pragma once

#include "scene/SceneState.h"

#include <cstdint>
#include <vector>

namespace garment_attachment_target_builder {

std::vector<GarmentAttachmentConstraint> build_garment_attachment_targets(
    const GarmentObject& garment,
    const CharacterMesh& character_mesh,
    std::uint32_t character_frame_index,
    const std::vector<std::uint32_t>& character_triangle_indices);

}
